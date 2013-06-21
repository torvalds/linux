/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_power.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 14:34
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#include "super_i.h"


static inline int check_range(struct axp_info *info,__s32 voltage)
{
	if (voltage < info->min_uV || voltage > info->max_uV)
		return -1;

	return 0;
}

static int axp20_ldo4_data[] = {
    1250, 1300, 1400, 1500, 1600, 1700,
    1800, 1900, 2000, 2500, 2700, 2800,
    3000, 3100, 3200, 3300
};

static struct axp_info axp20_info[] = {
	AXP(POWER_VOL_LDO1,	 AXP20LDO1,	AXP20LDO1,	  0, AXP20_LDO1,  0, 0),//ldo1 for rtc
	AXP(POWER_VOL_LDO2,	      1800,      3300,  100, AXP20_LDO2,  4, 4),//ldo2 for analog1
	AXP(POWER_VOL_LDO3,	       700,      3500,   25, AXP20_LDO3,  0, 7),//ldo3 for digital
	AXP(POWER_VOL_LDO4,	      1250,      3300,  100, AXP20_LDO4,  0, 4),//ldo4 for analog2
	AXP(POWER_VOL_DCDC2,       700,      2275,   25, AXP20_BUCK2, 0, 6),//buck2 for core
	AXP(POWER_VOL_DCDC3,       700,      3500,   25, AXP20_BUCK3, 0, 7),//buck3 for memery
};

static inline struct axp_info *find_info(int id)
{
	struct axp_info *ri;
	int i;

	for (i = 0; i < sizeof(axp20_info)/sizeof(struct axp_info); i++) {
		ri = &axp20_info[i];
		if (ri->id == id)
			return ri;
	}
	return 0;
}

/*
*********************************************************************************************************
*                           mem_set_voltage
*
*Description: set voltage for mem;
*
*Arguments  : type      voltage type, defined as "enum power_vol_type_e";
*             voltage   voltage value, based on "mv";
*
*Return     : 0: succeed;
*			-1: failed.
*Notes      :
*
*********************************************************************************************************
*/
__s32  mem_set_voltage(enum power_vol_type_e type, __s32 voltage)
{
	struct axp_info *info = 0;
	__u8 val, mask, reg_val;
	__s32 ret = 0;

	info = find_info(type);
	if (info == 0) {
		return -1;
	}

	if (check_range(info, voltage)) {
		return -1;
	}

	if (type != POWER_VOL_LDO4)
		val = raw_lib_udiv((voltage-info->min_uV+info->step_uV-1), info->step_uV);
	else{
		if(voltage == 1250000 ){
			val = 0;
		}
		else{
			val = raw_lib_udiv((voltage-1200000+info->step_uV-1), info->step_uV);
			if(val > 16){
				val = val - 6;
			}
			else if(val > 13){
				val = val - 5;
			}
			else if(val > 12){
				val = val - 4;
			}
			else if(val > 8)
				val = 8;
		}
	}


	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	if( 0 != twi_byte_rw(TWI_OP_RD,AXP_ADDR,info->vol_reg, &reg_val)){
		return -1;
	}

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		if(0 != twi_byte_rw(TWI_OP_WR,AXP_ADDR,info->vol_reg, &reg_val)){
			return -1;
		}
	}

	return ret;
}


/*
*********************************************************************************************************
*                           mem_get_voltage
*
*Description: get voltage for mem;
*
*Arguments  : type  voltage type, defined as "enum power_vol_type_e";
*
*Return     : voltage value, based on "mv";
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 mem_get_voltage(enum power_vol_type_e type)
{
	struct axp_info *info = 0;
	__u8 val, mask;

	info = find_info(type);
	if (info == 0) {
		return -1;
	}

	if(twi_byte_rw(TWI_OP_RD,AXP_ADDR,info->vol_reg, &val)){
			return -1;
	}

	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = (val & mask) >> info->vol_shift;
	if (type != POWER_VOL_LDO4)
		return info->min_uV + info->step_uV * val;
	else
		return axp20_ldo4_data[val]*1000;
}


/*
*********************************************************************************************************
*                           mem_power_init
*
* Description: init power for mem.
*
* Arguments  : none;
*
* Returns    : 0: succeed;
*			 -1: failed;
*********************************************************************************************************
*/
__s32 mem_power_init(__u32 wakeup_src)
{
	__u8 reg_val;

	/* enable power key long/short */
	if(wakeup_src & AXP_WAKEUP_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x03;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
	}

	/*enable power key short: bit1*/
	if(wakeup_src & AXP_WAKEUP_SHORT_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x02;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
	}

	/*enable power key long: bit0*/
	if(wakeup_src & AXP_WAKEUP_LONG_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x01;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
	}	

	/*°Ñ44H¼Ä´æÆ÷µÄbit6(°´¼üÉÏÉýÑØ´¥·¢)ÖÃ1*/
	if(wakeup_src & AXP_WAKEUP_ASCEND){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x40;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
	}
	
	/*°Ñ44H¼Ä´æÆ÷µÄbit5(ÏÂ½µÑØ´¥·¢)ÖÃ1*/
	if(wakeup_src & AXP_WAKEUP_DESCEND){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x20;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
	}
	
	/* enable low voltage warning */
	if(wakeup_src & AXP_WAKEUP_LOWBATT){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN4, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}
			reg_val |= 0x03;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN4, &reg_val)){
                printk("mem_power_init. %d\n", __LINE__);
				return -1;
			}

			/* clear pending */
			reg_val = 0x03;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQ4, &reg_val)){
				return -1;
			}
	}
		
	return 0;
}

/*
*********************************************************************************************************
*                           mem_power_exit
*
* Description: exit power for mem.
*
* Arguments  : none;
*
* Returns    : 0: succeed;
*			 -1: failed;
*********************************************************************************************************
*/
__s32 mem_power_exit(__u32 wakeup_src)
{
	__u8    reg_val;

	//setup_env();
	/* disable power key long/short */
	if(wakeup_src & AXP_WAKEUP_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
				return -1;
			}
			reg_val &= ~0x03;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
				return -1;
			}
	}

	/*enable power key short: bit1*/
	if(wakeup_src & AXP_WAKEUP_SHORT_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
				return -1;
			}
			reg_val &= ~0x02;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
				return -1;
			}
	}

	/*enable power key long: bit0*/
	if(wakeup_src & AXP_WAKEUP_LONG_KEY){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val)){
				return -1;
			}
			reg_val &= ~0x01;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val) ){
				return -1;
			}
	}	

	/*°Ñ44H¼Ä´æÆ÷µÄbit6(°´¼üÉÏÉýÑØ´¥·¢)ÖÃ1*/
	if(wakeup_src & AXP_WAKEUP_ASCEND){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
				return -1;
			}
			reg_val &= ~0x40;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
				return -1;
			}
	}
	
	/*°Ñ44H¼Ä´æÆ÷µÄbit5(ÏÂ½µÑØ´¥·¢)ÖÃ1*/
	if(wakeup_src & AXP_WAKEUP_DESCEND){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
				return -1;
			}
			reg_val &= ~0x20;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN5, &reg_val)){
				return -1;
			}
	}
		
	/* enable low voltage warning */
	if(wakeup_src & AXP_WAKEUP_LOWBATT){
			if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN4, &reg_val)){
				return -1;
			}
			reg_val &= ~0x03;
			if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN4, &reg_val)){
				return -1;
			}

	}
	
	return 0;
}

/*
 * mem_power_off
 *
 * Description:config wakeup signal. 
 *             turn off power for cpu-1v2, int-1v2, vcc-3v3 , but avcc-3v , dram-1v5.
 *             turn off 2*[csi ldo(low dropout regulator)] 
 *
 * Arguments  : none;
 *
 * Returns	  : 0: succeed;
 *			   -1: failed;
 */
__s32 mem_power_off(void)
{
	__u8 reg_val = 0;
#if 1
	/*config wakeup signal*/
	/*°Ñ31H¼Ä´æÆ÷µÄbit3(°´¼ü¡¢gpio»½ÐÑÎ»)ÖÃ1: low battery to power off*/
	if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,0x31, &reg_val)){
		return -1;
	}
	reg_val |= 0x08;
	if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,0x31, &reg_val)){
		return -1;
	}

#if 1
	/*power off*/
	/*°Ñ12H¼Ä´æÆ÷µÄbit0¡¢1¡¢3¡¢4¡¢6ÖÃ0*/
	if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
	reg_val &= ~0x5b;
	if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
#endif

#if 0
	/*power off*/
	/*°Ñ12H¼Ä´æÆ÷µÄbit1¡¢3¡¢4¡¢6ÖÃ0*/
	if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
	reg_val &= ~0x5a;
	if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
#endif

#endif
	/* cpu enter sleep, wait wakeup by interrupt */
	//busy_waiting();
	asm("WFI");

	/*never get here.
	 *when reach here, mean twi transfer err, and cpu are not shut down.
	 * wfi have been  changed.
	 */
	while(1);
	
	return -1;
}

/*
 * mem_power_off_nommu
 *
 * Description:config wakeup signal. 
 *             turn off power for cpu-1v2, int-1v2, vcc-3v3 , but avcc-3v , dram-1v5.
 *             turn off 2*[csi ldo(low dropout regulator)] 
 *
 * Arguments  : none;
 *
 * Returns	  : 0: succeed;
 *			  -1: failed;
 */
__s32 mem_power_off_nommu(void)
{
	__u8 reg_val = 0;
#if 1
	/*config wakeup signal*/
	/*°Ñ31H¼Ä´æÆ÷µÄbit3(°´¼ü¡¢gpio»½ÐÑÎ»)ÖÃ1*/
	if(0 != twi_byte_rw_nommu(TWI_OP_RD, AXP_ADDR,0x31, &reg_val)){
		//print_call_info_nommu();
		return -1;
	}
	reg_val |= 0x08;
	if(twi_byte_rw_nommu(TWI_OP_WR, AXP_ADDR,0x31, &reg_val)){
		return -2;
	}

#if 1
	/*power off*/
	//printk_nommu("notify pmu to power off. \n");
	/*°Ñ12H¼Ä´æÆ÷µÄbit0¡¢1¡¢3¡¢4¡¢6ÖÃ0*/
	if(twi_byte_rw_nommu(TWI_OP_RD, AXP_ADDR,0x12, &reg_val)){
		//print_call_info_nommu();
		return -3;
	}
	reg_val &= ~0x5b;
	if(twi_byte_rw_nommu(TWI_OP_WR, AXP_ADDR,0x12, &reg_val)){
		return -4;
	}
#endif

#if 0
	/*power off*/
	/*°Ñ12H¼Ä´æÆ÷µÄbit1¡¢3¡¢4¡¢6ÖÃ0*/
	if(twi_byte_rw(TWI_OP_RD, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
	reg_val &= ~0x5a;
	if(twi_byte_rw(TWI_OP_WR, AXP_ADDR,0x12, &reg_val)){
		return -1;
	}
#endif

#endif

	/* cpu enter sleep, wait wakeup by interrupt */
	asm("WFI");

	/*never get here.
	 *when reach here, mean twi transfer err, and cpu are not shut down.
	 * wfi have been  changed.
	 */
	while(1);
	
	return -5;
}



