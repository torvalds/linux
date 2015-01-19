#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <mach/am_regs.h>
#include <mach/gpio.h>
#include <linux/amlogic/ricoh_pmu.h>
#ifdef CONFIG_AML_DVFS
#include <linux/amlogic/aml_dvfs.h>
#endif

#ifdef CONFIG_AMLOGIC_USB
static struct notifier_block rn5t618_otg_nb;                            // notifier_block for OTG issue
static struct notifier_block rn5t618_usb_nb;                            // notifier_block for USB charger issue
extern int dwc_otg_power_register_notifier(struct notifier_block *nb);
extern int dwc_otg_power_unregister_notifier(struct notifier_block *nb);
extern int dwc_otg_charger_detect_register_notifier(struct notifier_block *nb);
extern int dwc_otg_charger_detect_unregister_notifier(struct notifier_block *nb);
#endif

struct i2c_client *g_rn5t618_client = NULL; 
static const struct i2c_device_id ricoh_pmu_id_table[] = {
#ifdef CONFIG_RN5T618
	{ "rn5t618", 0},
#endif
	{},
};
MODULE_DEVICE_TABLE(i2c, ricoh_pmu_id_table);

static const char *ricoh_pmu_sub_driver[] = {
#ifdef CONFIG_RN5T618
    RN5T618_DRIVER_NAME,
#endif
};

#ifdef CONFIG_OF
#define DEBUG_TREE      0
#define DEBUG_PARSE     0
#define DBG(format, args...) printk("[RICOH]%s, "format, __func__, ##args)

/*
 * must make sure value is 32 bit when use this macro
 * otherwise you should use another variable to get result value
 */
#define PARSE_UINT32_PROPERTY(node, prop_name, value, exception)        \
    if (of_property_read_u32(node, prop_name, (u32*)(&value))) {        \
        DBG("failed to get property: %s\n", prop_name);                 \
        goto exception;                                                 \
    }                                                                   \
    if (DEBUG_PARSE) {                                                  \
        DBG("get property:%25s, value:0x%08x, dec:%8d\n",               \
            prop_name, value, value);                                   \
    }

#define PARSE_STRING_PROPERTY(node, prop_name, value, exception)            \
    if (of_property_read_string(node, prop_name, (const char **)&value)) {  \
        DBG("failed to get property: %s\n", prop_name);                     \
        goto exception;                                                     \
    }                                                                       \
    if (DEBUG_PARSE) {                                                      \
        DBG("get property:%25s, value:%s\n",                                \
            prop_name, value);                                              \
    }

#define ALLOC_DEVICES(return_pointer, size, flag)                       \
    return_pointer = kzalloc(size, flag);                               \
    if (!return_pointer) {                                              \
        DBG("%d, allocate "#return_pointer" failed\n", __LINE__);       \
        return -ENOMEM;                                                 \
    } 
    
#if DEBUG_TREE
char msg_buf[100];
static void scan_node_tree(struct device_node *top_node, int off)
{
    if (!top_node) {
        return;    
    }
    if (!off) {
        printk("device tree is :\n");
    }
    while (top_node) {
        memset(msg_buf, ' ', sizeof(msg_buf));
        sprintf(msg_buf + off, "|--%s\n", top_node->name);
        printk(msg_buf);
        scan_node_tree(top_node->child, off + 4);
        top_node = top_node->sibling;
    }
}
#endif

static int setup_supply_data(struct device_node *node, struct ricoh_pmu_init_data *s_data)
{
    int err;
    struct device_node *b_node;
    struct battery_parameter *battery;
    phandle fhandle;

    err = of_property_read_bool(node, "reset-to-system");
    if (err) {
        s_data->reset_to_system = 1;    
    }
    PARSE_UINT32_PROPERTY(node, "soft_limit_to99", s_data->soft_limit_to99, parse_failed);
    PARSE_UINT32_PROPERTY(node, "board_battery",   fhandle,                 parse_failed);
    PARSE_UINT32_PROPERTY(node, "vbus_dcin_short_connect", s_data->vbus_dcin_short_connect, parse_failed);
    b_node = of_find_node_by_phandle(fhandle);
    if (!b_node) {
        DBG("find battery node failed, current:%s\n", node->name);
    }
    ALLOC_DEVICES(battery, sizeof(*battery), GFP_KERNEL);
    if (parse_battery_parameters(b_node, battery)) {
        DBG("failed to parse battery parameter, node:%s\n", b_node->name);
        kfree(battery);
    } else {
        s_data->board_battery = battery;                                // attach to axp_supply_init_data 
    }
    return 0;

parse_failed:
    return -EINVAL;
}

static int setup_platform_pmu_init_data(struct device_node *node, struct ricoh_pmu_init_data *pdata)
{
    if (setup_supply_data(node, pdata)) {
        return  -EINVAL; 
    }
#if 0                                       // not used right now
    /*
     * if there are not assigned propertys of dc2/dc3 voltage, just leave them to 
     * default value.
     */
    if (buck2) {
        PARSE_UINT32_PROPERTY(node, "ddr_voltage", tmp, setup1);
        buck2->constraints.state_standby.uV = tmp;
    }
setup1:
    if (buck3) {
        PARSE_UINT32_PROPERTY(node, "vddao_voltage", tmp, setup2);
        buck3->constraints.state_standby.uV = tmp;
    }
setup2:
#endif

    return 0;
}

static struct i2c_device_id *find_id_table_by_name(const struct i2c_device_id *look_table, char *name)
{
    while (look_table->name && look_table->name[0]) {
        if (!strcmp(look_table->name, name)) {
            return (struct i2c_device_id *)look_table;    
        }
        look_table++;
    }
    return NULL;
}
static struct ricoh_pmu_init_data *init_data;
#endif /* CONFIG_OF */

#if defined(CONFIG_AML_DVFS) && defined(CONFIG_RN5T618)
int convert_id_to_dcdc(uint32_t id)
{
    int dcdc = 0; 
    switch (id) {
    case AML_DVFS_ID_VCCK:
        dcdc = 1;
        break;

    case AML_DVFS_ID_VDDEE:
        dcdc = 2;
        break;

    case AML_DVFS_ID_DDR:
        dcdc = 3;
        break;

    default:
        break;
    }
    return dcdc;
}

static int rn5t618_set_voltage(uint32_t id, uint32_t min_uV, uint32_t max_uV)
{
    int dcdc = convert_id_to_dcdc(id);
    uint32_t vol = 0;
    
    if (min_uV > max_uV) {
        return -1;    
    }
    vol = (min_uV + max_uV) / 2;
    if (dcdc >= 1 && dcdc <= 3) {
        return rn5t618_set_dcdc_voltage(dcdc, vol);
    }
    return -EINVAL;
}

static int rn5t618_get_voltage(uint32_t id, uint32_t *uV)
{
    int dcdc = convert_id_to_dcdc(id);

    if (dcdc >= 1 && dcdc <= 3) {
        return rn5t618_get_dcdc_voltage(dcdc, uV);    
    }

    return -EINVAL;
}

struct aml_dvfs_driver rn5t618_dvfs_driver = {
    .name        = "rn5t618-dvfs",
    .id_mask     = (AML_DVFS_ID_VCCK | AML_DVFS_ID_VDDEE | AML_DVFS_ID_DDR),
    .set_voltage = rn5t618_set_voltage, 
    .get_voltage = rn5t618_get_voltage,
};
#endif
extern struct aml_pmu_driver rn5t618_pmu_driver;

static int ricoh_pmu_check_device(struct i2c_client *client)
{
    int ret = -1;

    ret = i2c_smbus_read_byte(client);
    if (ret < 0) {
        RICOH_DBG("%s, i2c xfer failed, ret:%d\n", __func__, ret);
    }
    return ret;
}

static int ricoh_pmu_probe(struct i2c_client *client,
                           const struct i2c_device_id *id)
{
#ifdef CONFIG_OF
    char   *sub_type = NULL;
#endif
    struct i2c_device_id *type = NULL;
    struct platform_device *pdev;
	int ret;
    
    ret = ricoh_pmu_check_device(client);                               // check client device first
    if (ret < 0) {
        return ret; 
    }
#ifdef CONFIG_OF
#if DEBUG_TREE
    scan_node_tree(client->dev.of_node, 0);
#endif
    init_data = kzalloc(sizeof(*init_data), GFP_KERNEL);
    if (!init_data) {
        DBG("%s, allocate initialize data failed\n", __func__);
        return -ENOMEM;    
    }
    setup_platform_pmu_init_data(client->dev.of_node, init_data);
    PARSE_STRING_PROPERTY(client->dev.of_node, "sub_type", sub_type, out_free_chip);
    type = find_id_table_by_name(ricoh_pmu_id_table, sub_type);
    if (!type) {                                                        // sub type is not supported
        DBG("sub_type of '%s' is not match, abort\n", sub_type);
        goto out_free_chip; 
    }
#else
#ifdef CONFIG_RN5T618 
    type = find_id_table_by_name(ricoh_pmu_id_table, "rn5t618");
#endif
#endif /* CONFIG_OF */

#ifdef CONFIG_RN5T618
    if (type->driver_data == 0) {
        g_rn5t618_client = client;            
    #if defined(CONFIG_AML_DVFS) && defined(CONFIG_RN5T618)
        aml_dvfs_register_driver(&rn5t618_dvfs_driver);
    #endif
        aml_pmu_register_driver(&rn5t618_pmu_driver);
    #ifdef CONFIG_AMLOGIC_USB
        rn5t618_otg_nb.notifier_call = rn5t618_otg_change;
        rn5t618_usb_nb.notifier_call = rn5t618_usb_charger;
        dwc_otg_power_register_notifier(&rn5t618_otg_nb);
        dwc_otg_charger_detect_register_notifier(&rn5t618_usb_nb);
    #endif
        RICOH_DBG("%s, %d\n", __func__, __LINE__);
        rn5t618_get_saved_coulomb();
    }
#endif
    /*
     * allocate and regist devices, then kernel will probe drivers
     */
    pdev = platform_device_alloc(ricoh_pmu_sub_driver[type->driver_data], 0);
    if (pdev == NULL) {
        printk(">> %s, allocate platform device failed\n", __func__);
        return -ENOMEM;
    }
    pdev->dev.parent        = &client->dev;
    pdev->dev.platform_data =  init_data; 
    ret = platform_device_add(pdev);
    printk(KERN_DEBUG "%s, %d\n", __func__, __LINE__);
    if (ret) {
        printk(">> %s, add platform device failed\n", __func__);
        platform_device_del(pdev);
        return -EINVAL;
    }
    i2c_set_clientdata(client, pdev); 

out_free_chip:

	return 0;
}

static int ricoh_pmu_remove(struct i2c_client *client)
{
    struct platform_device *pdev = i2c_get_clientdata(client);

#ifdef CONFIG_RN5T618
    g_rn5t618_client = NULL;
#if defined(CONFIG_AML_DVFS) && defined(CONFIG_RN5T618)
    aml_dvfs_unregister_driver(&rn5t618_dvfs_driver);
#endif
    aml_pmu_clear_driver();
#ifdef CONFIG_AMLOGIC_USB
    dwc_otg_power_unregister_notifier(&rn5t618_otg_nb);
    dwc_otg_charger_detect_unregister_notifier(&rn5t618_usb_nb);
#endif  /* CONFIG_AMLOGIC_USB */
#endif  /* CONFIG_RN5T618     */

    platform_device_del(pdev);
#ifdef CONFIG_OF
    kfree(init_data);
#endif

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ricoh_pmu_match_id = {
    .compatible = "ricoh_pmu",
};
#endif

static struct i2c_driver ricoh_pmu_driver = {
	.driver	= {
		.name	= "ricoh_pmu",
		.owner	= THIS_MODULE,
    #ifdef CONFIG_OF
        .of_match_table = &ricoh_pmu_match_id,
    #endif
	},
	.probe		= ricoh_pmu_probe,
	.remove		= ricoh_pmu_remove,
	.id_table	= ricoh_pmu_id_table,
};

static int __init ricoh_pmu_init(void)
{
	return i2c_add_driver(&ricoh_pmu_driver);
}
subsys_initcall(ricoh_pmu_init);

static void __exit ricoh_pmu_exit(void)
{
	i2c_del_driver(&ricoh_pmu_driver);
}
module_exit(ricoh_pmu_exit);

MODULE_DESCRIPTION("RICOH PMU device driver");
MODULE_AUTHOR("tao.zeng@amlogic.com");
MODULE_LICENSE("GPL");
