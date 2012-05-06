#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>

#include <mach/sram.h>

#define	TWL60xx_IRQ_BASE	(NR_GIC_IRQS + NR_GPIO_IRQS  )
#ifdef CONFIG_TWL4030_CORE
#define	TWL60xx_BASE_NR_IRQS	24
#else
#define	TWL60xx_BASE_NR_IRQS	0
#endif
#define TWL60xx_IRQ_END		(TWL60xx_IRQ_BASE + TWL60xx_BASE_NR_IRQS)


#ifdef CONFIG_TWL4030_CORE
#define VREG_VOLTAGE		3
#define VREG_VOLTAGE_DVS_SMPS 3 
static inline int twl_reg_read(unsigned base, unsigned slave_subgp, unsigned offset)
{
	u8 value;
	int status;
	status = twl_i2c_read_u8(slave_subgp,&value, base + offset);
	return (status < 0) ? status : value;
}


static inline int twl_reg_write(unsigned base, unsigned slave_subgp, unsigned offset,
						 u8 value)
{
	return twl_i2c_write_u8(slave_subgp,value, base + offset);
}

int tps80032_pre_init(void){
	int ret;
	u8 value;
	printk("%s\n", __func__);

	return 0;

}
int tps80032_set_init(void)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	printk("%s\n", __func__);
	
	ldo = regulator_get(NULL, "ldo1");	//vcca_33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo1 vcca_33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo4");	// vdd_11
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_enable(ldo);
//	printk("%s set ldo4 vdd_11=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	dcdc = regulator_get(NULL, "smps4");
	regulator_set_voltage(dcdc,3000000,3000000);
	regulator_enable(dcdc); 
//	printk("%s set dcdc4 vcc_io=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	ldo = regulator_get(NULL, "ldo2");	// vdd_usb11
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_enable(ldo);
//	printk("%s set ldo2 vdd_usb11=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo5");	// vcc_25
	regulator_set_voltage(ldo, 2500000, 2500000);
	regulator_enable(ldo);
//	printk("%s set ldo5 vcc_25=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldousb");	// vcc_usb33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldousb vcc_usb33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	dcdc = regulator_get(NULL, "vdd_cpu"); // vdd_arm
	regulator_set_voltage(dcdc,1100000,1100000);
	regulator_enable(dcdc); 
	printk("%s set dcdc1 vdd_cpu=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "vdd_core");  //vdd_log
	regulator_set_voltage(dcdc,1100000,1100000);
	regulator_enable(dcdc); 
	printk("%s set dcdc2 vdd_core=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "smps3");  //vcc_ddr
	regulator_set_voltage(dcdc,1800000,1800000);
	regulator_enable(dcdc); 
//	printk("%s set dcdc3 vcc_lpddr2_1v8=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "smps5");
	regulator_set_voltage(dcdc,1200000,1200000);
	regulator_enable(dcdc); 
//	printk("%s set dcdc5 vcc_lpddr2_1v2=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	
	ldo = regulator_get(NULL, "ldo3");	//vcc_nandflash
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo3 vcc_nandflash=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo6");	//codecvdd_1v8
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_enable(ldo);
//	printk("%s set ldo6 codecvdd_1v8=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo7");	//vcc_lcd
	regulator_set_voltage(ldo, 3000000, 3000000);
	regulator_enable(ldo);
//	printk("%s set ldo7 vcc_lcd=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldoln");	  //vcccodec_io
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldoln vcccodec_io=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
/*
	ldo = regulator_get(NULL, "vana");	//vana_out
	regulator_set_voltage(ldo, 2500000, 2500000);
//	regulator_set_suspend_voltage(ldo, 2500000);
	regulator_enable(ldo);
	printk("%s set vana vana_out=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
*/

	printk("tps80032_set_init end.\n");
	return 0;
}


static struct regulator_consumer_supply tps80032_smps1_supply[] = {
	{
		.supply = "vdd_cpu",
	},
};
static struct regulator_consumer_supply tps80032_smps2_supply[] = {
	{
		.supply = "vdd_core",
	},
};
static struct regulator_consumer_supply tps80032_smps3_supply[] = {
	{
		.supply = "smps3",
	},
};
static struct regulator_consumer_supply tps80032_smps4_supply[] = {
	{
		.supply = "smps4",
	},
};
static struct regulator_consumer_supply tps80032_smps5_supply[] = {
	{
		.supply = "smps5",
	},
};
static struct regulator_consumer_supply tps80032_ldo1_supply[] = {
	{
		.supply = "ldo1",
	},
};
static struct regulator_consumer_supply tps80032_ldo2_supply[] = {
	{
		.supply = "ldo2",
	},
};

static struct regulator_consumer_supply tps80032_ldo3_supply[] = {
	{
		.supply = "ldo3",
	},
};
static struct regulator_consumer_supply tps80032_ldo4_supply[] = {
	{
		.supply = "ldo4",
	},
};
static struct regulator_consumer_supply tps80032_ldo5_supply[] = {
	{
		.supply = "ldo5",
	},
};
static struct regulator_consumer_supply tps80032_ldo6_supply[] = {
	{
		.supply = "ldo6",
	},
};
static struct regulator_consumer_supply tps80032_ldo7_supply[] = {
	{
		.supply = "ldo7",
	},
};

static struct regulator_consumer_supply tps80032_ldoln_supply[] = {
	{
		.supply = "ldoln",
	},
};
static struct regulator_consumer_supply tps80032_ldousb_supply[] = {
	{
		.supply = "ldousb",
	},
};
static struct regulator_consumer_supply tps80032_ldovana_supply[] = {
	{
		.supply = "vana",
	},
};
/* */
static struct regulator_init_data tps80032_smps1 = {
	.constraints = {
		.name           = "SMPS1",
		.min_uV			= 600000,
		.max_uV			= 2100000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_smps1_supply),
	.consumer_supplies =  tps80032_smps1_supply,
};

/* */
static struct regulator_init_data tps80032_smps2 = {
	.constraints = {
		.name           = "SMPS2",
		.min_uV			= 600000,
		.max_uV			= 2100000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_smps2_supply),
	.consumer_supplies =  tps80032_smps2_supply,
};



/* */
static struct regulator_init_data tps80032_smps3 = {
	.constraints = {
		.name           = "SMPS3",
		.min_uV			= 600000,
		.max_uV			= 2100000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_smps3_supply),
	.consumer_supplies =  tps80032_smps3_supply,
};


/* */
static struct regulator_init_data tps80032_smps4 = {
	.constraints = {
		.name           = "SMPS4",
		.min_uV			= 600000,
		.max_uV			= 2100000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_smps4_supply),
	.consumer_supplies =  tps80032_smps4_supply,
};
/* */
static struct regulator_init_data tps80032_smps5 = {
	.constraints = {
		.name           = "SMPS5",
		.min_uV			= 600000,
		.max_uV			= 2100000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_smps5_supply),
	.consumer_supplies =  tps80032_smps5_supply,
};
static struct regulator_init_data tps80032_ldo1 = {
	.constraints = {
		.name           = "LDO1",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo1_supply),
	.consumer_supplies =  tps80032_ldo1_supply,
};

/* */
static struct regulator_init_data tps80032_ldo2 = {
	.constraints = {
		.name           = "LDO2",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo2_supply),
	.consumer_supplies =  tps80032_ldo2_supply,
};

/* */
static struct regulator_init_data tps80032_ldo3 = {
	.constraints = {
		.name           = "LDO3",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo3_supply),
	.consumer_supplies =  tps80032_ldo3_supply,
};

/* */
static struct regulator_init_data tps80032_ldo4 = {
	.constraints = {
		.name           = "LDO4",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo4_supply),
	.consumer_supplies =  tps80032_ldo4_supply,
};

/* */
static struct regulator_init_data tps80032_ldo5 = {
	.constraints = {
		.name           = "LDO5",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo5_supply),
	.consumer_supplies =  tps80032_ldo5_supply,
};

/* */
static struct regulator_init_data tps80032_ldo6 = {
	.constraints = {
		.name           = "LDO6",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo6_supply),
	.consumer_supplies =  tps80032_ldo6_supply,
};

/* */
static struct regulator_init_data tps80032_ldo7 = {
	.constraints = {
		.name           = "LDO7",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldo7_supply),
	.consumer_supplies =  tps80032_ldo7_supply,
};

/* */
static struct regulator_init_data tps80032_ldoln = {
	.constraints = {
		.name           = "LDOLN",
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldoln_supply),
	.consumer_supplies =  tps80032_ldoln_supply,
};

/* */
static struct regulator_init_data tps80032_ldousb = {
	.constraints = {
		.name           = "LDOUSB",
		.min_uV			= 3300000,
		.max_uV			= 3300000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldousb_supply),
	.consumer_supplies =  tps80032_ldousb_supply,
};

/* */
static struct regulator_init_data tps80032_ldovana = {
	.constraints = {
		.name           = "LDOVANA",
		.min_uV			= 600000,
		.max_uV			= 2500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(tps80032_ldovana_supply),
	.consumer_supplies =  tps80032_ldovana_supply,
};


static struct twl4030_platform_data tps80032_data = {
	.irq_base	= TWL60xx_IRQ_BASE,
	.irq_end	= TWL60xx_IRQ_END,
	//.irq            = RK29_PIN0_PA1,
	.pre_init = tps80032_pre_init,
	.set_init = tps80032_set_init,

	/* Regulators */
	.ldo1		= &tps80032_ldo1,
	.ldo2		= &tps80032_ldo2,
	.ldo3		= &tps80032_ldo3,
	.ldo4		= &tps80032_ldo4,
	.ldo5		= &tps80032_ldo5,
	.ldo6		= &tps80032_ldo6,
	.ldo7		= &tps80032_ldo7,
	.ldoln             = &tps80032_ldoln,
	.ldousb           =&tps80032_ldousb,
	.vana              = &tps80032_ldovana,
	
	.smps1 = &tps80032_smps1,
	.smps2= &tps80032_smps2,
	.smps3          = &tps80032_smps3,
	.smps4          = &tps80032_smps4,
	.smps5   = &tps80032_smps5,
 
};

#endif
