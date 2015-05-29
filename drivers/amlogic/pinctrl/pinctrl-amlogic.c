/*
 * Driver for the amlogic pin controller
 *
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/device.h>
#include <plat/io.h>
#include <mach/am_regs.h>
#include "../../pinctrl/core.h"
#include <linux/amlogic/pinctrl-amlogic.h>
#include <linux/pinctrl/pinctrl-state.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/vmalloc.h>

//#define AML_PIN_DEBUG_GUP
const char *pctdev_name;
//#define debug
#ifdef debug
#define dbg_print(...) {printk("===%s===%d===\n",__FUNCTION__,__LINE__);printk(__VA_ARGS__);}
#else
#define dbg_print(...)
#endif
extern unsigned int p_pin_mux_reg_addr[];
extern unsigned p_pull_up_addr[];
extern unsigned p_gpio_oen_addr[];

struct amlogic_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct amlogic_pinctrl_soc_data *soc;
	unsigned int pinmux_cell;
};

/******************************************************************/
static int amlogic_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	return apmx->soc->ngroups;
}

static const char *amloigc_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned group)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	return apmx->soc->groups[group].name;
}

static int amlogic_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *num_pins)
{
	struct amlogic_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	*pins = pmx->soc->groups[selector].pins;
	*num_pins = pmx->soc->groups[selector].num_pins;
	return 0;
}
#if 0
static void amlogic_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}
#endif
#if CONFIG_OF
void amlogic_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map, unsigned num_maps)
{
	u32 i;
	for (i = 0; i < num_maps; i++) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	}
	kfree(map);
}



int amlogic_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct pinctrl_map *new_map=NULL;
	unsigned new_num = 1;
	unsigned long config = 0;
	unsigned long *pconfig;
	const char *pinctrl_set = "amlogic,setmask";
	const char *pinctrl_clr = "amlogic,clrmask";
	bool purecfg = false;
	u32 val, reg;
	int ret, i = 0;

	/* Check for pin config node which has no 'reg' property */
	if (of_property_read_u32(np, pinctrl_set, &reg)&&
			of_property_read_u32(np, pinctrl_clr, &val))
		purecfg = true;

	ret = of_property_read_u32(np, "amlogic,pullup", &val);
	if (!ret)
		config = AML_PINCONF_PACK_PULL(AML_PCON_PULLUP,val);
	ret = of_property_read_u32(np, "amlogic,pullupen", &val);
	if (!ret)
		config |= AML_PINCONF_PACK_PULLEN(AML_PCON_PULLUP,val);
	ret = of_property_read_u32(np, "amlogic,enable-output", &val);
	if (!ret)
		config |= AML_PINCONF_PACK_ENOUT(AML_PCON_ENOUT,val);

	/* Check for group node which has both mux and config settings */
	if (!purecfg&&config)
		new_num =2;

	new_map = kzalloc(sizeof(*new_map) * new_num,GFP_KERNEL);
	if (!new_map){
		printk("vmalloc map fail\n");
		return -ENOMEM;
	}

	if (config) {
		pconfig = kmemdup(&config, sizeof(config), GFP_KERNEL);
		if (!pconfig) {
			ret = -ENOMEM;
			goto free_group;
		}

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin = np->name;
		new_map[i].data.configs.configs = pconfig;
		new_map[i].data.configs.num_configs = 1;
		i++;
	}

	if (!purecfg) {
		new_map[i].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[i].data.mux.function = np->name;
		new_map[i].data.mux.group = np->name;
	}

	*map = new_map;
	*num_maps = new_num;

	return 0;

free_group:
	kfree(new_map);
	return ret;
}
#else
int amlogic_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	return 0;
}
void amlogic_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map, unsigned num_maps)
{
	return ;
}

#endif
static struct pinctrl_ops amlogic_pctrl_ops = {
	.get_groups_count = amlogic_get_groups_count,
	.get_group_name = amloigc_get_group_name,
	.get_group_pins =amlogic_get_group_pins,
	.dt_node_to_map = amlogic_pinctrl_dt_node_to_map,
	.dt_free_map = amlogic_pinctrl_dt_free_map,
};

static int amlogic_pmx_enable(struct pinctrl_dev *pctldev, unsigned selector,
			   unsigned group)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	int i;
	struct amlogic_pin_group *pin_group=&apmx->soc->groups[group];
	struct amlogic_reg_mask *setmask=pin_group->setmask;
	struct amlogic_reg_mask *clrmask=pin_group->clearmask;
	for(i=0;i<pin_group->num_clearmask;i++){
		aml_clr_reg32_mask(p_pin_mux_reg_addr[clrmask[i].reg],clrmask[i].mask);
		dbg_print("clear reg=%x,mask=%x\n",clrmask[i].reg,clrmask[i].mask);
	}
	for(i=0;i<pin_group->num_setmask;i++){
		aml_set_reg32_mask(p_pin_mux_reg_addr[setmask[i].reg],setmask[i].mask);
		dbg_print("set reg=%d,mask=0x%x\n",setmask[i].reg,setmask[i].mask);
	}
	return 0;
}

static void amlogic_pmx_disable(struct pinctrl_dev *pctldev, unsigned selector,
			     unsigned group)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	int i;
	struct amlogic_pin_group *pin_group=&apmx->soc->groups[group];
	struct amlogic_reg_mask *setmask=pin_group->setmask;
	//struct amlogic_reg_mask *clrmask=pin_group->clearmask;
#if 0
	for(i=0;i<pin_group->num_clearmask;i++){
		aml_set_reg32_mask(p_pin_mux_reg_addr[clrmask[i].reg],clrmask[i].mask);
		dbg_print("clear reg=%x,mask=%x\n",clrmask[i].reg,clrmask[i].mask);
	}
#endif
	for(i=0;i<pin_group->num_setmask;i++){
		aml_clr_reg32_mask(p_pin_mux_reg_addr[setmask[i].reg],setmask[i].mask);
		dbg_print("set reg=%d,mask=0x%x\n",setmask[i].reg,setmask[i].mask);
       }
	return;
}

static int amlogic_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	return apmx->soc->nfunctions;
}

static const char *amlogic_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct amlogic_pmx *apmx=pinctrl_dev_get_drvdata(pctldev);
	return apmx->soc->functions[selector].name;
}

static int amlogic_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	struct amlogic_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	*groups = pmx->soc->functions[selector].groups;
	*num_groups = pmx->soc->functions[selector].num_groups;
	return 0;
}
static int amlogic_gpio_request_enable (struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned offset)
{
	struct pin_desc *desc;
	desc = pin_desc_get(pctldev, offset);
	if(desc->mux_owner){
		printk("%s is using the pin %s as pinmux\n",desc->mux_owner,desc->name);
		return -EINVAL;
	}
	return 0;
}
static int amlogic_pinctrl_request(struct pinctrl_dev *pctldev, unsigned offset)
{
	struct pin_desc *desc;
	desc = pin_desc_get(pctldev, offset);
	if(desc->gpio_owner){
		printk("%s is using the pin %s as gpio\n",desc->gpio_owner,desc->name);
		return -EINVAL;
	}
	return 0;
}
static struct pinmux_ops amlogic_pmx_ops = {
	.get_functions_count = amlogic_pmx_get_funcs_count,
	.get_function_name = amlogic_pmx_get_func_name,
	.get_function_groups = amlogic_pmx_get_groups,
	.enable = amlogic_pmx_enable,
	.disable = amlogic_pmx_disable,
	.gpio_request_enable=amlogic_gpio_request_enable,
	.request=amlogic_pinctrl_request,
};

/*
 * GPIO ranges handled by the application-side amlogic GPIO controller
 * Very many pins can be converted into GPIO pins, but we only list those
 * that are useful in practice to cut down on tables.
 */

static struct pinctrl_gpio_range amlogic_gpio_ranges = {
	.name="amlogic:gpio",
	.id=0,
	.base=0,
	.pin_base=0,
};


int amlogic_pin_config_get(struct pinctrl_dev *pctldev,
			unsigned pin,
			unsigned long *config)
{
	return 0;
}

int amlogic_pin_config_set(struct pinctrl_dev *pctldev,
			unsigned pin,
			unsigned long config)
{
	return 0;
}
int amlogic_pin_config_group_set(struct pinctrl_dev *pctldev,
			unsigned group,
			unsigned long config)
{
	unsigned reg,bit,ret=-1,i;
	struct amlogic_pmx *apmx = pinctrl_dev_get_drvdata(pctldev);
	u16 pullparam = AML_PINCONF_UNPACK_PULL_PARA(config);
	u16 oenparam = AML_PINCONF_UNPACK_ENOUT_PARA(config);
	u16 oenarg = AML_PINCONF_UNPACK_ENOUT_ARG(config);
	const struct amlogic_pin_group *pin_group=&apmx->soc->groups[group];
	const unsigned int *pins=pin_group->pins;
	const unsigned int num_pins=pin_group->num_pins;
	dbg_print("config=0x%x\n",config);
	if(AML_PCON_PULLUP==pullparam)
	{
		for(i=0;i<num_pins;i++){
			ret=apmx->soc->meson_set_pullup(pins[i],config);
		}
	}
	if(AML_PCON_ENOUT==oenparam)
	{
		for(i=0;i<num_pins;i++){			
			ret=apmx->soc->pin_map_to_direction(pins[i],&reg,&bit);
			dbg_print("pin[%d]=%d,reg=%d,bit=%d\n",i,pins[i],reg,bit);
			if(!ret)
			{
				if(oenarg)
					aml_set_reg32_mask(p_gpio_oen_addr[reg],1<<bit);
				else
					aml_clr_reg32_mask(p_gpio_oen_addr[reg],1<<bit);
			}
		}
	}
	return ret;
}

static struct pinconf_ops amlogic_pconf_ops = {
	.pin_config_set = amlogic_pin_config_set,
	.pin_config_get=amlogic_pin_config_get,
	.pin_config_group_set = amlogic_pin_config_group_set,
};

static struct pinctrl_desc amlogic_pmx_desc = {
	.name = "amlogic pinmux",
	.pctlops = &amlogic_pctrl_ops,
	.pmxops = &amlogic_pmx_ops,
	.confops = &amlogic_pconf_ops,
	.owner = THIS_MODULE,
};

struct pinctrl_map * amlogic_register_mux_map(struct device *dev,const char *state_name,
					const char *func,const char *group)
{
	struct pinctrl_map * map1;
	int ret;
	map1=kzalloc(sizeof(struct pinctrl_map),GFP_KERNEL);
	if(!(map1)){
		printk("%s:mallo error\n",__func__);
		return  NULL;
	}
	map1->dev_name=dev_name(dev);
	map1->ctrl_dev_name=pctdev_name;
	map1->type=PIN_MAP_TYPE_MUX_GROUP;
	if(state_name)
		map1->name=state_name;
	else
		map1->name=PINCTRL_STATE_DEFAULT;
	map1->data.mux.function=func;
	map1->data.mux.group=group;
	ret=pinctrl_register_map(map1, 1, false);
	if(!ret)
		return map1;
	else
		kfree(map1);
	return NULL;
}
EXPORT_SYMBOL(amlogic_register_mux_map);

void amlogic_unregister_mux_map(struct pinctrl_map *map)
{
	 pinctrl_unregister_map(map);
	 kfree(map);
}
EXPORT_SYMBOL(amlogic_unregister_mux_map);

struct pinctrl_map * amlogic_register_config_map(struct device *dev,const char *state_name,const char *group,
					unsigned long *configs,unsigned int num_configs)
{
	struct pinctrl_map * map1;
	int ret;
	map1=kzalloc(sizeof(struct pinctrl_map),GFP_KERNEL);
	if(!(map1)){
		printk("%s:mallo error\n",__func__);
		return  NULL;
	}
	map1->dev_name=dev_name(dev);
	map1->ctrl_dev_name=pctdev_name;
	map1->type=PIN_MAP_TYPE_CONFIGS_GROUP;
	if(state_name)
		map1->name=state_name;
	else
		map1->name=PINCTRL_STATE_DEFAULT;
	map1->data.configs.group_or_pin=group;
	map1->data.configs.configs=configs;
	map1->data.configs.num_configs=num_configs;
	ret=pinctrl_register_map(map1, 1, false);
	if(!ret)
		return map1;
	else
		kfree(map1);
	return NULL;
}
EXPORT_SYMBOL(amlogic_register_config_map);
void amlogic_unregister_config_map(struct pinctrl_map *map)
{
	pinctrl_unregister_map(map);
	 kfree(map);
}
EXPORT_SYMBOL(amlogic_unregister_config_map);

static int amlogic_pinctrl_parse_group(struct platform_device *pdev,
				   struct device_node *np, int idx,
				   const char **out_name)
{
	struct amlogic_pmx *d = platform_get_drvdata(pdev);
	struct amlogic_pin_group *g = d->soc->groups;
	struct property *prop;
	const char *propname = "amlogic,pins";
	const char *pinctrl_set = "amlogic,setmask";
	const char *pinctrl_clr = "amlogic,clrmask";
	const char *gpioname;
	int length,ret=0,i;
	u32 val;
	g=g+idx;
	g->name = np->name;
#if 0
/*read amlogic pins through num*/
	prop = of_find_property(np, propname, &length);
	if (!prop)
		return -EINVAL;
	g->num_pins = length / sizeof(u32);

	g->pins = devm_kzalloc(&pdev->dev, g->num_pins * sizeof(*g->pins),
			       GFP_KERNEL);
	if (!g->pins)
		return -ENOMEM;

	ret=of_property_read_u32_array(np, propname, g->pins, g->num_pins);
	if (ret)
			return -EINVAL;
#endif
/*read amlogic pins through name*/
	g->num_pins=of_property_count_strings(np, propname);
	if(g->num_pins>0){
		g->pins = devm_kzalloc(&pdev->dev, g->num_pins * sizeof(*g->pins),
				       GFP_KERNEL);
		if (!g->pins){
			ret= -ENOMEM;			
			dev_err(&pdev->dev, "malloc g->pins error\n");
			goto err;
		}
		for(i=0;i<g->num_pins;i++)
		{
			ret = of_property_read_string_index(np, propname,
							    i, &gpioname);
			if(ret<0){
				ret= -EINVAL;
				dev_err(&pdev->dev, "read %s error\n",propname);
				goto err;
			}
			ret=amlogic_gpio_name_map_num(gpioname);
			if(ret<0){
				ret= -EINVAL;
				dev_err(&pdev->dev, "%s change name to num  error\n",gpioname);
				goto err;
			}
			g->pins[i]=ret;
		}
	}
/*read amlogic set mask*/
	if(!of_property_read_u32(np, pinctrl_set, &val)){
		prop = of_find_property(np, pinctrl_set, &length);
		if (!prop){
			ret= -EINVAL;			
			dev_err(&pdev->dev, "read %s length error\n",pinctrl_set);
			goto err;
		}
		g->num_setmask=length / sizeof(u32);
		if(g->num_setmask%d->pinmux_cell){
			dev_err(&pdev->dev, "num_setmask error must be multiples of 2\n");
			g->num_setmask=(g->num_setmask/d->pinmux_cell)*d->pinmux_cell;
		}
		g->num_setmask=g->num_setmask/d->pinmux_cell;
		g->setmask= devm_kzalloc(&pdev->dev, g->num_setmask * sizeof(*g->setmask),
				       GFP_KERNEL);
		if (!g->setmask){
			ret= -ENOMEM;
			dev_err(&pdev->dev, "malloc g->setmask error\n");
			goto err;
		}

		ret=of_property_read_u32_array(np, pinctrl_set, (u32 *)(g->setmask), length / sizeof(u32));
		if (ret){
			ret= -EINVAL;		
			dev_err(&pdev->dev, "read %s data error\n",pinctrl_set);
			goto err;
		}
	}else{
		g->setmask=NULL;
		g->num_setmask=0;
	}
/*read clear mask*/
	if(!of_property_read_u32(np, pinctrl_clr, &val)){
		prop = of_find_property(np, pinctrl_clr, &length);
		if (!prop){
			dev_err(&pdev->dev, "read %s length error\n",pinctrl_clr);
			ret =-EINVAL;
			goto err;
		}
		g->num_clearmask=length / sizeof(u32);
		if(g->num_clearmask%d->pinmux_cell){
			dev_err(&pdev->dev, "num_setmask error must be multiples of 2\n");
			g->num_clearmask=(g->num_clearmask/d->pinmux_cell)*d->pinmux_cell;
		}
		g->num_clearmask=g->num_clearmask/d->pinmux_cell;
		g->clearmask= devm_kzalloc(&pdev->dev, g->num_clearmask * sizeof(*g->clearmask),
				       GFP_KERNEL);
		if (!g->clearmask){
			ret=-ENOMEM;		
			dev_err(&pdev->dev, "malloc g->clearmask error\n");
			goto err;
		}
		ret=of_property_read_u32_array(np, pinctrl_clr,(u32 *)( g->clearmask), length / sizeof(u32));
		if (ret){
			ret= -EINVAL;
			dev_err(&pdev->dev, "read %s data error\n",pinctrl_clr);
			goto err;
		}
	}
	else{
		g->clearmask=NULL;
		g->num_clearmask=0;
	}
	if (out_name)
		*out_name = g->name;
	return 0;
err:
	if(g->pins)
		devm_kfree(&pdev->dev,g->pins);
	if(g->setmask)
		devm_kfree(&pdev->dev,g->setmask);
	if(g->clearmask)
		devm_kfree(&pdev->dev,g->clearmask);
	return ret;
}

static int amlogic_pinctrl_probe_dt(struct platform_device *pdev,
				struct amlogic_pmx *d)
{
	struct amlogic_pinctrl_soc_data *soc = d->soc;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct amlogic_pmx_func *f;
	const char *pinctrl_set = "amlogic,setmask";
	const char *pinctrl_clr="amlogic,clrmask";
	const char *fn, *fnull = "";
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	child = of_get_next_child(np, NULL);
	if (!child) {
		dev_err(&pdev->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total functions and groups */
	fn = fnull;
	for_each_child_of_node(np, child) {
		soc->ngroups++;
		/* Skip pure pinconf node */
		if (of_property_read_u32(child, pinctrl_set, &val)&&
			of_property_read_u32(child, pinctrl_clr, &val))
			continue;
		if (strcmp(fn, child->name)) {
			fn = child->name;
			soc->nfunctions++;
		}
	}

	soc->functions = devm_kzalloc(&pdev->dev, soc->nfunctions *
				      sizeof(*soc->functions), GFP_KERNEL);
	if (!soc->functions){
		dev_err(&pdev->dev, "malloc soc->functions error\n");
		ret=-ENOMEM;
		goto err;
	}
	soc->groups = devm_kzalloc(&pdev->dev, soc->ngroups *
				   sizeof(*soc->groups), GFP_KERNEL);
	if (!soc->groups){		
		dev_err(&pdev->dev, "malloc soc->functions error\n");
		ret=-ENOMEM;
		goto err;
	}
	/* Count groups for each function */
	fn = fnull;
	f = &soc->functions[idxf];
	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, pinctrl_set, &val)&&
			of_property_read_u32(child, pinctrl_clr, &val))
			continue;
		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->name = fn = child->name;
		}
		f->num_groups++;
	};
	/* Get groups for each function */
	idxf = 0;
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, pinctrl_set, &val)&&
			of_property_read_u32(child, pinctrl_clr, &val)) {
			ret = amlogic_pinctrl_parse_group(pdev, child,
						      idxg++, NULL);
			if (ret)
				goto err;
			continue;
		}

		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->groups = devm_kzalloc(&pdev->dev, f->num_groups *
						 sizeof(*f->groups),
						 GFP_KERNEL);
			if (!f->groups){
				dev_err(&pdev->dev, "malloc f->groups error\n");
				ret=-ENOMEM;
				goto err;
			}
			fn = child->name;
			i = 0;
		}
		ret = amlogic_pinctrl_parse_group(pdev, child, idxg++,
					      &f->groups[i++]);
		if (ret)
			goto  err;
	}
	return 0;
err:
	if(soc->groups)
		devm_kfree(&pdev->dev,soc->groups);
	if(soc->groups)
		devm_kfree(&pdev->dev,soc->groups);
	if(soc->functions)
		devm_kfree(&pdev->dev,soc->functions);
	return ret;
}
#ifdef AML_PIN_DEBUG_GUP
#include <linux/amlogic/gpio-amlogic.h>
extern struct amlogic_gpio_desc amlogic_pins[];
static void amlogic_dump_pinctrl_data(struct platform_device *pdev)
{
	struct amlogic_pmx *d = platform_get_drvdata(pdev);
	struct amlogic_pinctrl_soc_data *soc=d->soc;
	struct amlogic_pmx_func *func=soc->functions;
	struct amlogic_pin_group *groups=soc->groups;
	char **group;
	int i,j;
	for(i=0;i<soc->nfunctions;func++,i++)
	{
		printk("function name:%s\n",func->name);
		group=(char **)(func->groups);
		for(j=0;j<func->num_groups;group++,j++)
		{	
			printk("\tgroup in function:%s\n",group[j]);
		}
	}
	for(i=0;i<soc->ngroups;groups++,i++)
	{
		printk("group name:%s\n",groups->name);
		for(j=0;j<groups->num_pins;j++)
		{
			printk("\t");
			printk("pin num=%s",amlogic_pins[groups->pins[j]].name);
		}
		printk("\n");
		for(j=0;j<groups->num_setmask;j++)
		{
			printk("\t");
			printk("set reg=%d,mask=%x",groups->setmask[j].reg,groups->setmask[j].mask);
		}
		printk("\n");
		for(j=0;j<groups->num_clearmask;j++)
		{
			printk("\t");
			printk("clear reg=%d,mask=%x",groups->clearmask[j].reg,groups->clearmask[j].mask);
		}
		printk("\n");
	}
}
#endif
struct pinctrl_dev *pctl;

int  amlogic_pmx_probe(struct platform_device *pdev,struct amlogic_pinctrl_soc_data *soc_data)
{
	struct amlogic_pmx *apmx;
	int ret,val;
	printk("Init pinux probe!\n");
	/* Create state holders etc for this driver */
	apmx = devm_kzalloc(&pdev->dev, sizeof(*apmx), GFP_KERNEL);
	if (!apmx) {
		dev_err(&pdev->dev, "Can't alloc amlogic_pmx\n");
		return -ENOMEM;
	}
	apmx->dev = &pdev->dev;
	apmx->soc = soc_data;
	platform_set_drvdata(pdev, apmx);
	
	ret=of_property_read_u32(pdev->dev.of_node, "#pinmux-cells", &val);
	if(ret){
		dev_err(&pdev->dev, "dt probe #pinmux-cells failed: %d\n", ret);
		goto err;
	}
	apmx->pinmux_cell=val;
	
	ret=amlogic_pinctrl_probe_dt(pdev,apmx);
	if (ret) {
		dev_err(&pdev->dev, "dt probe failed: %d\n", ret);
		goto err;
	}
#ifdef AML_PIN_DEBUG_GUP
	amlogic_dump_pinctrl_data(pdev);
#endif	
	amlogic_gpio_ranges.npins = apmx->soc->npins;
	amlogic_pmx_desc.name = dev_name(&pdev->dev);
	amlogic_pmx_desc.pins = apmx->soc->pins;
	amlogic_pmx_desc.npins = apmx->soc->npins;

	apmx->pctl = pinctrl_register(&amlogic_pmx_desc, &pdev->dev, apmx);

	if (!apmx->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		goto err;
	}
	pinctrl_add_gpio_range(apmx->pctl, &amlogic_gpio_ranges);
	pctdev_name=dev_name(&pdev->dev);
	pinctrl_provide_dummies();
	dev_info(&pdev->dev, "Probed amlogic pinctrl driver\n");
	pctl=apmx->pctl;

	return 0;
err:
	devm_kfree(&pdev->dev,apmx);
	return ret;
}
EXPORT_SYMBOL_GPL(amlogic_pmx_probe);

int amlogic_pmx_remove(struct platform_device *pdev)
{
	struct amlogic_pmx *pmx = platform_get_drvdata(pdev);
	pinctrl_unregister(pmx->pctl);
	return 0;
}
EXPORT_SYMBOL_GPL(amlogic_pmx_remove);

