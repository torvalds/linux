#include "rk_hdmi.h"
#ifdef CONFIG_OF
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

/* rk hdmi power control parse from dts
 *
*/
int rk_hdmi_pwr_ctr_parse_dt(struct hdmi *dev_drv)
{
	struct device_node *root = of_find_node_by_name(dev_drv->dev->of_node,
							"power_ctr_hdmi");
	struct device_node *child;
	struct rk_disp_pwr_ctr_list *pwr_ctr;
	struct list_head *pos;
	enum of_gpio_flags flags;
	u32 val = 0;
	u32 debug = 0;
	int ret;

	INIT_LIST_HEAD(&dev_drv->pwrlist_head);
	if (!root) {
		dev_err(dev_drv->dev, "can't find power_ctr node %d\n",
			dev_drv->id);
		return -ENODEV;
	}

	for_each_child_of_node(root, child) {
		pwr_ctr = kmalloc(sizeof(struct rk_disp_pwr_ctr_list),
				  GFP_KERNEL);
		strcpy(pwr_ctr->pwr_ctr.name, child->name);
		if (!of_property_read_u32(child, "rockchip,power_type", &val)) {
			if (val == GPIO) {
				pwr_ctr->pwr_ctr.type = GPIO;
				pwr_ctr->pwr_ctr.gpio =
				    of_get_gpio_flags(child, 0, &flags);
				if (!gpio_is_valid(pwr_ctr->pwr_ctr.gpio)) {
					dev_err(dev_drv->dev,
						"%s ivalid gpio\n",
						child->name);
					return -EINVAL;
				}
				pwr_ctr->pwr_ctr.atv_val =
				    flags & OF_GPIO_ACTIVE_LOW;
				ret = gpio_request(pwr_ctr->pwr_ctr.gpio,
						   child->name);
				if (ret) {
					dev_err(dev_drv->dev,
						"request %s gpio fail:%d\n",
						child->name, ret);
					return -1;
				}

			} else {
				pwr_ctr->pwr_ctr.type = REGULATOR;

			}
		};
		of_property_read_u32(child, "rockchip,delay", &val);
		pwr_ctr->pwr_ctr.delay = val;
		of_property_read_u32(child, "rockchip,is_rst", &val);
		pwr_ctr->pwr_ctr.is_rst = val;
		list_add_tail(&pwr_ctr->list, &dev_drv->pwrlist_head);
	}

	of_property_read_u32(root, "rockchip,debug", &debug);

	if (debug) {
		list_for_each(pos, &dev_drv->pwrlist_head) {
			pwr_ctr = list_entry(pos, struct rk_disp_pwr_ctr_list,
					     list);
			dev_info(dev_drv->dev, "pwr_ctr_name:%s\n"
				 "pwr_type:%s\n" "gpio:%d\n"
				 "atv_val:%d\n" "delay:%d\n\n",
				 pwr_ctr->pwr_ctr.name,
				 (pwr_ctr->pwr_ctr.type == GPIO) ?
				 "gpio" : "regulator",
				 pwr_ctr->pwr_ctr.gpio,
				 pwr_ctr->pwr_ctr.atv_val,
				 pwr_ctr->pwr_ctr.delay);
		}
	}

	return 0;

}

int rk_hdmi_pwr_enable(struct hdmi *dev_drv)
{
	struct list_head *pos;
	struct rk_disp_pwr_ctr_list *pwr_ctr_list;
	struct pwr_ctr *pwr_ctr;

	if (list_empty(&dev_drv->pwrlist_head))
		return 0;

	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list,
					  list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_direction_output(pwr_ctr->gpio, pwr_ctr->atv_val);
			mdelay(pwr_ctr->delay);
			if (pwr_ctr->is_rst == 1) {
				if (pwr_ctr->atv_val == 1)
					gpio_set_value(pwr_ctr->gpio, 0);
				else
					gpio_set_value(pwr_ctr->gpio, 1);

				mdelay(pwr_ctr->delay);
			}
		}
	}

	return 0;
}

int rk_hdmi_pwr_disable(struct hdmi *dev_drv)
{
	struct list_head *pos;
	struct rk_disp_pwr_ctr_list *pwr_ctr_list;
	struct pwr_ctr *pwr_ctr;

	if (list_empty(&dev_drv->pwrlist_head))
		return 0;

	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list,
					  list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_set_value(pwr_ctr->gpio, pwr_ctr->atv_val);
			if (pwr_ctr->is_rst == 1) {
				if (pwr_ctr->atv_val == 1)
					gpio_set_value(pwr_ctr->gpio, 0);
				else
					gpio_set_value(pwr_ctr->gpio, 1);
			}
		}
	}

	return 0;
}

int rk_hdmi_parse_dt(struct hdmi *hdmi_drv)
{
	struct device_node *np = hdmi_drv->dev->of_node;
	int ret = 0, gpio = 0;

	if (!np) {
		dev_err(hdmi_drv->dev, "could not find hdmi node\n");
		return -1;
	}

	gpio = of_get_named_gpio(np, "rockchips,hdmi_irq_gpio", 0);
	if (!gpio_is_valid(gpio))
		dev_info(hdmi_drv->dev, "invalid hdmi_irq_gpio: %d\n", gpio);
	hdmi_drv->irq = gpio;

	ret = rk_hdmi_pwr_ctr_parse_dt(hdmi_drv);

	return ret;
}

#else
int rk_hdmi_pwr_enable(struct hdmi *dev_drv)
{
	return 0;
}

int rk_hdmi_pwr_disable(struct hdmi *dev_drv)
{
	return 0;
}

int rk_hdmi_parse_dt(struct hdmi *hdmi_drv)
{
	return 0;
}
#endif
