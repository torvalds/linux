#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>

#include <mach/sram.h>

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

#define CRU_CLKGATE5_CON_ADDR 0x00e4
#define GRF_GPIO6L_DIR_ADDR 0x0030
#define GRF_GPIO6L_DO_ADDR 0x0068
#define GRF_GPIO6L_EN_ADDR 0x00a0
#define GPIO6_PB3_DIR_OUT  0x08000800
#define GPIO6_PB3_DO_LOW  0x08000000
#define GPIO6_PB3_DO_HIGH  0x08000800
#define GPIO6_PB3_EN_MASK  0x08000800
#define GPIO6_PB3_UNEN_MASK  0x08000000

#define	TWL60xx_IRQ_BASE	(NR_GIC_IRQS + NR_GPIO_IRQS  )
#ifdef CONFIG_TWL4030_CORE
#define	TWL60xx_BASE_NR_IRQS	24
#else
#define	TWL60xx_BASE_NR_IRQS	0
#endif
#define TWL60xx_IRQ_END		(TWL60xx_IRQ_BASE + TWL60xx_BASE_NR_IRQS)

#ifdef CONFIG_TWL4030_CORE
#define PREQ1_RES_ASS_A 0x2a
#define PREQ1_RES_ASS_B 0x2b
#define PREQ1_RES_ASS_C 0x2c
#define PREQ2_RES_ASS_A 0x2d
#define PREQ3_RES_ASS_A 0x30
#define PHOENIX_MSK_TRANSITION 0x01
#define PHOENIX_SENS_TRANSITION 0x0b
#define SMPS4_CFG_TRANS 0x11
#define SMPS4_CFG_STATE 0x12
#define SMPS4_CFG_VOLTAGE 0x13
#define SMPS5_CFG_TRANS 0x17
#define SMPS5_CFG_STATE 0x18
#define SMPS5_CFG_FORCE 0x19
#define SMPS5_CFG_VOLTAGE 0x1A
#define SMPS5_CFG_STEP 0x1B
#define SMPS1_CFG_TRANS 0x23
#define SMPS1_CFG_STATE 0x24
#define SMPS1_CFG_FORCE 0x25
#define SMPS1_CFG_VOLTAGE 0x26
#define SMPS1_CFG_STEP 0x27
#define SMPS2_CFG_TRANS 0x29
#define SMPS2_CFG_STATE 0x2a
#define SMPS2_CFG_FORCE 0x2b
#define SMPS2_CFG_VOLTAGE 0x2c
#define SMPS2_CFG_STEP 0x2d
#define VANA_CFG_TRANS 0x51
#define VANA_CFG_STATE 0x52
#define VANA_CFG_VOLTAGE 0x53
#define LDO2_CFG_TRANS 0x55
#define LDO2_CFG_STATE 0x56
#define LDO2_CFG_VOLTAGE 0x57
#define LDO4_CFG_TRANS 0x59
#define LDO4_CFG_STATE 0x5a
#define LDO4_CFG_VOLTAGE 0x5b
#define LDO3_CFG_TRANS 0x5d
#define LDO3_CFG_STATE 0x5e
#define LDO3_CFG_VOLTAGE 0x5f
#define LDO6_CFG_TRANS 0x61
#define LDO6_CFG_STATE 0x62
#define LDO6_CFG_VOLTAGE 0x63
#define LDOLN_CFG_TRANS 0x65
#define LDOLN_CFG_STATE 0x66
#define LDOLN_CFG_VOLTAGE 0x67
#define LDO5_CFG_TRANS 0x69
#define LDO5_CFG_STATE 0x6A
#define LDO5_CFG_VOLTAGE 0x6B
#define LDO1_CFG_TRANS 0x6D
#define LDO1_CFG_STATE 0x6E
#define LDO1_CFG_VOLTAGE 0x6F
#define LDOUSB_CFG_TRANS 0x71
#define LDOUSB_CFG_STATE 0x72
#define LDOUSB_CFG_VOLTAGE 0x73
#define LDO7_CFG_TRANS 0x75
#define LDO7_CFG_STATE 0x76
#define LDO7_CFG_VOLTAGE 0x77
#define CLK32KG_CFG_STATE  0x11
#define CLK32KAUDIO_CFG_STATE 0x14
static inline int twl_reg_read(unsigned base, unsigned slave_subgp)
{
	u8 value;
	int status;
	status = twl_i2c_read_u8(slave_subgp,&value, base);
	return (status < 0) ? status : value;
}


static inline int twl_reg_write(unsigned base, unsigned slave_subgp,
						 u8 value)
{
	return twl_i2c_write_u8(slave_subgp,value, base);
}

#define PMU_POWER_SLEEP RK30_PIN6_PB3	

int tps80032_pre_init(void){
	
	printk("%s\n", __func__);	

	gpio_request(PMU_POWER_SLEEP, "NULL");
	gpio_direction_output(PMU_POWER_SLEEP, GPIO_LOW);
	
	twl_reg_write(PREQ1_RES_ASS_A,TWL_MODULE_PM_SLAVE_RES,0x0b);
	twl_reg_write(PREQ1_RES_ASS_B,TWL_MODULE_PM_SLAVE_RES,0x10);
	twl_reg_write(PREQ1_RES_ASS_C,TWL_MODULE_PM_SLAVE_RES,0x27);
	twl_reg_write(PHOENIX_MSK_TRANSITION,TWL_MODULE_PM_MASTER,0x00);
	twl_reg_write(PHOENIX_SENS_TRANSITION,TWL_MODULE_PM_MASTER,0xc0);   //set pmu enter sleep on a preq1 rising edge
	
	twl_reg_write(SMPS1_CFG_STATE,TWL_MODULE_PM_RECEIVER,0x01);   //set state
	twl_reg_write(SMPS2_CFG_STATE,TWL_MODULE_PM_RECEIVER,0x01);
	twl_reg_write(LDO7_CFG_STATE,TWL_MODULE_PM_RECEIVER,0x01);

	twl_reg_write(CLK32KG_CFG_STATE,TWL_MODULE_PM_SLAVE_RES,0x01);  //set clk32kg on when we use
	twl_reg_write(CLK32KAUDIO_CFG_STATE,TWL_MODULE_PM_SLAVE_RES,0x01);  //set clk32kaudio on when we use
	
//	twl_reg_write(LDO5_CFG_TRANS,TWL_MODULE_PM_RECEIVER,0x03);   //set ldo5 is disabled when in sleep mode 
//	twl_reg_write(LDO7_CFG_TRANS,TWL_MODULE_PM_RECEIVER,0x03);   //set ldo7 is disabled when in sleep mode

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

	dcdc = regulator_get(NULL, "vcc_io");
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

	dcdc = regulator_get(NULL, "vcc_lpddr2_1v8");  //vcc_lpddr2_1v8
	regulator_set_voltage(dcdc,1800000,1800000);
	regulator_enable(dcdc); 
//	printk("%s set dcdc3 vcc_lpddr2_1v8=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "vcc_lpddr2_1v2");
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
		.supply = "smps1",
	},
	{
		.supply = "vdd_cpu",
	},
};
static struct regulator_consumer_supply tps80032_smps2_supply[] = {
	{
		.supply = "smps2",
	},
	{
		.supply = "vdd_core",
	},
};
static struct regulator_consumer_supply tps80032_smps3_supply[] = {
	{
		.supply = "smps3",
	},
	{
		.supply = "vcc_lpddr2_1v8",
	},
};
static struct regulator_consumer_supply tps80032_smps4_supply[] = {
	{
		.supply = "smps4",
	},
	{
		.supply = "vcc_io",
	},
};
static struct regulator_consumer_supply tps80032_smps5_supply[] = {
	{
		.supply = "smps5",
	},
	{
		.supply = "vcc_lpddr2_1v2",
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

static struct twl4030_madc_platform_data tps80032_madc_data = {
	.irq_line	= 1,
};
static int tps_batt_table[] = {
/* 0 C*/
	3400,3420,3440,3475,3505,3525,
	3540,3557,3570,3580,3610,
	3630,3640,3652,3662,3672,
	3680,3687,3693,3699,3705,
	3710,3714,3718,3722,3726,
	3730,3734,3738,3742,3746,
	3750,3756,3764,3774,3786,
	3800,3808,3817,3827,3845,
	3950,3964,3982,4002,4026,
	4030,4034,4055,4070,4085,4120
};
static struct twl4030_bci_platform_data tps80032_bci_data = {
	.battery_tmp_tbl	= tps_batt_table,
	.tblsize		= ARRAY_SIZE(tps_batt_table),
};

static int rk30_phy_init(struct device *dev){
	return 0;
	}
static int rk30_phy_exit(struct device *dev){
	return 0;
	}
static int rk30_phy_power(struct device *dev, int ID, int on){
	return 0;
	}
static int rk30_phy_set_clk(struct device *dev, int on){
	return 0;
	}
static int rk30_phy_suspend(struct device *dev, int suspend){
	return 0;
	}
static struct twl4030_usb_data tps80032_usbphy_data = {
	.phy_init	= rk30_phy_init,
	.phy_exit	= rk30_phy_exit,
	.phy_power	= rk30_phy_power,
	.phy_set_clock	= rk30_phy_set_clk,
	.phy_suspend	= rk30_phy_suspend,

};
static struct twl4030_ins sleep_on_seq[] __initdata = {
/*
 * Turn off everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_SLEEP), 2},
};

static struct twl4030_script sleep_on_script __initdata = {
	.script = sleep_on_seq,
	.size   = ARRAY_SIZE(sleep_on_seq),
	.flags  = TWL4030_SLEEP_SCRIPT,
};

static struct twl4030_ins wakeup_seq[] __initdata = {
/*
 * Reenable everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_script __initdata = {
	.script	= wakeup_seq,
	.size	= ARRAY_SIZE(wakeup_seq),
	.flags	= TWL4030_WAKEUP12_SCRIPT,
};

static struct twl4030_ins wakeup_p3_seq[] __initdata = {
/*
 * Reenable everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_p3_script __initdata = {
	.script	= wakeup_p3_seq,
	.size	= ARRAY_SIZE(wakeup_p3_seq),
	.flags	= TWL4030_WAKEUP3_SCRIPT,
};
static struct twl4030_ins wrst_seq[] __initdata = {
/*
 * Reset twl4030.
 * Reset VDD1 regulator.
 * Reset VDD2 regulator.
 * Reset VPLL1 regulator.
 * Enable sysclk output.
 * Reenable twl4030.
 */
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_OFF), 2},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 1, RES_STATE_ACTIVE),
		0x13},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_PP, 0, 3, RES_STATE_OFF), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD1, RES_STATE_WRST), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD2, RES_STATE_WRST), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VPLL1, RES_STATE_WRST), 0x35},
	{MSG_SINGULAR(DEV_GRP_P3, RES_HFCLKOUT, RES_STATE_ACTIVE), 2},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_ACTIVE), 2},
};
static struct twl4030_script wrst_script __initdata = {
	.script = wrst_seq,
	.size   = ARRAY_SIZE(wrst_seq),
	.flags  = TWL4030_WRST_SCRIPT,
};

static struct twl4030_script *twl4030_scripts[] __initdata = {
	/* wakeup12 script should be loaded before sleep script, otherwise a
	   board might hit retention before loading of wakeup script is
	   completed. This can cause boot failures depending on timing issues.
	*/
	&wakeup_script,
	&sleep_on_script,
	&wakeup_p3_script,
	&wrst_script,
};
static struct twl4030_resconfig twl4030_rconfig[] __initdata = {
	{ .resource = RES_VDD1, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VDD2, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VPLL1, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VPLL2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX1, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX3, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX4, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
		{ .resource = RES_VMMC1, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VMMC2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VDAC, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VSIM, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTANA1, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = -1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTANA2, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTDIG, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = -1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VIO, .devgroup = DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_CLKEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1 , .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_REGEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_NRES_PWRON, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_SYSEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_HFCLKOUT, .devgroup = DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_32KCLKOUT, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_RESET, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_MAIN_REF, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ 0, 0},
};
static struct twl4030_power_data tps80032_scripts_data __initdata = {
	.scripts        = twl4030_scripts,
	.num = ARRAY_SIZE(twl4030_scripts),
	.resource_config = twl4030_rconfig,
};


void __sramfunc board_pmu_suspend(void)
{	
	grf_writel(GPIO6_PB3_DIR_OUT, GRF_GPIO6L_DIR_ADDR);
	grf_writel(GPIO6_PB3_DO_HIGH, GRF_GPIO6L_DO_ADDR);  //set gpio6_b3 output low
	grf_writel(GPIO6_PB3_EN_MASK, GRF_GPIO6L_EN_ADDR);
}
void __sramfunc board_pmu_resume(void)
{
	grf_writel(GPIO6_PB3_DIR_OUT, GRF_GPIO6L_DIR_ADDR);
	grf_writel(GPIO6_PB3_DO_LOW, GRF_GPIO6L_DO_ADDR);     //set gpio6_b3 output high
	grf_writel(GPIO6_PB3_EN_MASK, GRF_GPIO6L_EN_ADDR);
	sram_udelay(2000);
}

static struct twl4030_platform_data tps80032_data = {
	.irq_base	= TWL60xx_IRQ_BASE,
	.irq_end	= TWL60xx_IRQ_END,
	//.irq            = RK29_PIN0_PA1,
	.pre_init = tps80032_pre_init,
	.set_init = tps80032_set_init,
	
	.madc		= &tps80032_madc_data,
	.bci		= &tps80032_bci_data,
	.usb		= &tps80032_usbphy_data,
//	.power			= &tps80032_scripts_data,
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
