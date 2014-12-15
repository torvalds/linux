#ifndef	_LINUX_AXP_SPLY_H_
#define	_LINUX_AXP_SPLY_H_

#include <linux/amlogic/aml_pmu_common.h>
/*      AXP18      */
#define	AXP18_STATUS						POWER18_STATUS
#define	AXP18_CHARGE_STATUS					POWER18_ONOFF
#define	AXP18_IN_CHARGE						(1 << 2)

#define	AXP18_CHARGE_CONTROL1				POWER18_CHARGE1
#define	AXP18_CHARGER_ENABLE				(1 << 7)
#define	AXP18_CHG_CURLIM_ENABLE				(1 << 3)
#define AXP18_CHARGE_CONTROL2				POWER18_CHARGE2

#define	AXP18_FAULT_LOG1					POWER18_INTSTS1
#define	AXP18_FAULT_LOG2					POWER18_INTSTS3
#define	AXP18_FAULT_LOG_BATINACT			(1 << 0)
#define	AXP18_FAULT_LOG_COLD				(1 << 1)
#define	AXP18_FAULT_LOG_OVER_TEMP			(1 << 2)
#define	AXP18_FAULT_LOG_VBAT_LOW			(1 << 6)
#define	AXP18_FAULT_LOG_VBAT_OVER			(1 << 7)

#define	AXP18_FINISH_CHARGE					(1 << 2)

#define	AXP18_ADC_CONTROL					POWER18_ADCSW_CTL
#define	AXP18_ADC_BATVOL_ENABLE				(1 << 7)
#define	AXP18_ADC_BATCUR_ENABLE				(1 << 6)
#define	AXP18_ADC_ACVOL_ENABLE				(1 << 5)
#define	AXP18_ADC_ACCUR_ENABLE				(1 << 4)

#define	AXP18_DATA_BUFFER1					POWER18_DATA_BUFFER1
#define	AXP18_DATA_BUFFER2					POWER18_DATA_BUFFER2

#define	AXP18_VBAT_RES						POWER18_BATTERY_VOL
#define	AXP18_IBAT_RES						POWER18_BATTERY_CURRENT
#define	AXP18_VAC_RES						POWER18_DCIN_VOL
#define	AXP18_IAC_RES						POWER18_DCIN_CURRENT

#define AXP18_CHARGE_VBUS					POWER18_IPS_SET

#define AXP20_VOL_MAX			12 // capability buffer length

static 	struct input_dev * powerkeydev;

#define AXP18_NOTIFIER_ON	(AXP18_IRQ_EXTOV | \
						     AXP18_IRQ_EXTIN | \
						     AXP18_IRQ_EXTRE | \
						     AXP18_IRQ_EXTLO | \
						     AXP18_IRQ_TEMOV | \
						     AXP18_IRQ_TEMLO | \
						     AXP18_IRQ_BATIN | \
						     AXP18_IRQ_BATRE | \
						     AXP18_IRQ_PEKLO | \
						     AXP18_IRQ_PEKSH )

/*      AXP19      */
#define AXP19_CHARGE_STATUS					POWER19_STATUS
#define AXP19_IN_CHARGE						(1 << 6)

#define AXP19_CHARGE_CONTROL1				POWER19_CHARGE1
#define AXP19_CHARGER_ENABLE				(1 << 7)
#define AXP19_CHARGE_CONTROL2				POWER19_CHARGE2
#define AXP19_BUCHARGE_CONTROL				POWER19_BACKUP_CHG
#define AXP19_BUCHARGER_ENABLE				(1 << 7)


#define AXP19_FAULT_LOG1					POWER19_MODE_CHGSTATUS
#define AXP19_FAULT_LOG_CHA_CUR_LOW			(1 << 2)
#define AXP19_FAULT_LOG_BATINACT			(1 << 3)

#define AXP19_FAULT_LOG_OVER_TEMP			(1 << 7)

#define AXP19_FAULT_LOG2					POWER19_INTSTS2
#define AXP19_FAULT_LOG_COLD				(1 << 0)

#define AXP19_FINISH_CHARGE					(1 << 2)


#define AXP19_ADC_CONTROL1					POWER19_ADC_EN1
#define AXP19_ADC_BATVOL_ENABLE				(1 << 7)
#define AXP19_ADC_BATCUR_ENABLE				(1 << 6)
#define AXP19_ADC_DCINVOL_ENABLE			(1 << 5)
#define AXP19_ADC_DCINCUR_ENABLE			(1 << 4)
#define AXP19_ADC_USBVOL_ENABLE				(1 << 3)
#define AXP19_ADC_USBCUR_ENABLE				(1 << 2)
#define AXP19_ADC_APSVOL_ENABLE				(1 << 1)
#define AXP19_ADC_TSVOL_ENABLE				(1 << 0)
#define AXP19_ADC_CONTROL2					POWER19_ADC_EN2
#define AXP19_ADC_INTERTEM_ENABLE			(1 << 7)

#define AXP19_ADC_GPIO0_ENABLE				(1 << 3)
#define AXP19_ADC_GPIO1_ENABLE				(1 << 2)
#define AXP19_ADC_GPIO2_ENABLE				(1 << 1)
#define AXP19_ADC_GPIO3_ENABLE				(1 << 0)
#define AXP19_ADC_CONTROL3					POWER19_ADC_SPEED


#define AXP19_VACH_RES						POWER19_ACIN_VOL_H8
#define AXP19_VACL_RES						POWER19_ACIN_VOL_L4
#define AXP19_IACH_RES						POWER19_ACIN_CUR_H8
#define AXP19_IACL_RES						POWER19_ACIN_CUR_L4
#define AXP19_VUSBH_RES						POWER19_VBUS_VOL_H8
#define AXP19_VUSBL_RES						POWER19_VBUS_VOL_L4
#define AXP19_IUSBH_RES						POWER19_VBUS_CUR_H8
#define AXP19_IUSBL_RES						POWER19_VBUS_CUR_L4
#define AXP19_TICH_RES						(0x5E)
#define AXP19_TICL_RES						(0x5F)

#define AXP19_TSH_RES						(0x62)
#define AXP19_ISL_RES						(0x63)
#define AXP19_VGPIO0H_RES					(0x64)
#define AXP19_VGPIO0L_RES					(0x65)
#define AXP19_VGPIO1H_RES					(0x66)
#define AXP19_VGPIO1L_RES					(0x67)
#define AXP19_VGPIO2H_RES					(0x68)
#define AXP19_VGPIO2L_RES					(0x69)
#define AXP19_VGPIO3H_RES					(0x6A)
#define AXP19_VGPIO3L_RES					(0x6B)

#define AXP19_PBATH_RES						POWER19_BAT_POWERH8
#define AXP19_PBATM_RES						POWER19_BAT_POWERM8
#define AXP19_PBATL_RES						POWER19_BAT_POWERL8

#define AXP19_VBATH_RES						POWER19_BAT_AVERVOL_H8
#define AXP19_VBATL_RES						POWER19_BAT_AVERVOL_L4
#define AXP19_ICHARH_RES					POWER19_BAT_AVERCHGCUR_H8
#define AXP19_ICHARL_RES					POWER19_BAT_AVERCHGCUR_L5
#define AXP19_IDISCHARH_RES					POWER19_BAT_AVERDISCHGCUR_H8
#define AXP19_IDISCHARL_RES					POWER19_BAT_AVERDISCHGCUR_L5
#define AXP19_VAPSH_RES						POWER19_APS_AVERVOL_H8
#define AXP19_VAPSL_RES						POWER19_APS_AVERVOL_L4


#define AXP19_COULOMB_CONTROL				POWER19_COULOMB_CTL
#define AXP19_COULOMB_ENABLE				(1 << 7)
#define AXP19_COULOMB_SUSPEND				(1 << 6)
#define AXP19_COULOMB_CLEAR					(1 << 5)

#define AXP19_CCHAR3_RES					POWER19_BAT_CHGCOULOMB3
#define AXP19_CCHAR2_RES					POWER19_BAT_CHGCOULOMB2
#define AXP19_CCHAR1_RES					POWER19_BAT_CHGCOULOMB1
#define AXP19_CCHAR0_RES					POWER19_BAT_CHGCOULOMB0
#define AXP19_CDISCHAR3_RES					POWER19_BAT_DISCHGCOULOMB3
#define AXP19_CDISCHAR2_RES					POWER19_BAT_DISCHGCOULOMB2
#define AXP19_CDISCHAR1_RES					POWER19_BAT_DISCHGCOULOMB1
#define AXP19_CDISCHAR0_RES					POWER19_BAT_DISCHGCOULOMB0

#define AXP19_DATA_BUFFER0					POWER19_DATA_BUFFER1
#define AXP19_DATA_BUFFER1					POWER19_DATA_BUFFER2
#define AXP19_DATA_BUFFER2					POWER19_DATA_BUFFER3
#define AXP19_DATA_BUFFER3					POWER19_DATA_BUFFER4

#define AXP19_CHARGE_VBUS					POWER19_IPS_SET

#define AXP19_CHARGE_LED					POWER19_OFF_CTL

#define AXP19_TIMER_CTL						POWER19_TIMER_CTL

#define AXP19_NOTIFIER_ON   		        (AXP19_IRQ_USBOV | \
										     AXP19_IRQ_USBIN | \
				        				     AXP19_IRQ_USBRE | \
				       					     AXP19_IRQ_USBLO | \
				       					     AXP19_IRQ_ACOV  | \
				       					     AXP19_IRQ_ACIN  | \
				       					     AXP19_IRQ_ACRE  | \
				       					     AXP19_IRQ_TEMOV | \
				       					     AXP19_IRQ_TEMLO | \
				       					     AXP19_IRQ_BATIN | \
				       					     AXP19_IRQ_BATRE | \
				       					     AXP19_IRQ_PEKLO | \
				       					     AXP19_IRQ_PEKSH) 


/*      AXP20      */
#define AXP20_CHARGE_STATUS					POWER20_STATUS
#define AXP20_IN_CHARGE						(1 << 6)

#define AXP20_CHARGE_CONTROL1				POWER20_CHARGE1
#define AXP20_CHARGER_ENABLE				(1 << 7)
#define AXP20_CHARGE_CONTROL2				POWER20_CHARGE2
#define AXP20_BUCHARGE_CONTROL				POWER20_BACKUP_CHG
#define AXP20_BUCHARGER_ENABLE				(1 << 7)


#define AXP20_FAULT_LOG1					POWER20_MODE_CHGSTATUS
#define AXP20_FAULT_LOG_CHA_CUR_LOW			(1 << 2)
#define AXP20_FAULT_LOG_BATINACT			(1 << 3)

#define AXP20_FAULT_LOG_OVER_TEMP			(1 << 7)

#define AXP20_FAULT_LOG2					POWER20_INTSTS2
#define AXP20_FAULT_LOG_COLD				(1 << 0)

#define AXP20_FINISH_CHARGE					(1 << 2)


#define AXP20_ADC_CONTROL1					POWER20_ADC_EN1
#define AXP20_ADC_BATVOL_ENABLE				(1 << 7)
#define AXP20_ADC_BATCUR_ENABLE				(1 << 6)
#define AXP20_ADC_DCINVOL_ENABLE			(1 << 5)
#define AXP20_ADC_DCINCUR_ENABLE			(1 << 4)
#define AXP20_ADC_USBVOL_ENABLE				(1 << 3)
#define AXP20_ADC_USBCUR_ENABLE				(1 << 2)
#define AXP20_ADC_APSVOL_ENABLE				(1 << 1)
#define AXP20_ADC_TSVOL_ENABLE				(1 << 0)
#define AXP20_ADC_CONTROL2					POWER20_ADC_EN2
#define AXP20_ADC_INTERTEM_ENABLE			(1 << 7)

#define AXP20_ADC_GPIO0_ENABLE				(1 << 3)
#define AXP20_ADC_GPIO1_ENABLE				(1 << 2)
#define AXP20_ADC_GPIO2_ENABLE				(1 << 1)
#define AXP20_ADC_GPIO3_ENABLE				(1 << 0)
#define AXP20_ADC_CONTROL3					POWER20_ADC_SPEED


#define AXP20_VACH_RES						POWER20_ACIN_VOL_H8
#define AXP20_VACL_RES						POWER20_ACIN_VOL_L4
#define AXP20_IACH_RES						POWER20_ACIN_CUR_H8
#define AXP20_IACL_RES						POWER20_ACIN_CUR_L4
#define AXP20_VUSBH_RES						POWER20_VBUS_VOL_H8
#define AXP20_VUSBL_RES						POWER20_VBUS_VOL_L4
#define AXP20_IUSBH_RES						POWER20_VBUS_CUR_H8
#define AXP20_IUSBL_RES						POWER20_VBUS_CUR_L4
#define AXP20_TICH_RES						(0x5E)
#define AXP20_TICL_RES						(0x5F)

#define AXP20_TSH_RES						(0x62)
#define AXP20_ISL_RES						(0x63)
#define AXP20_VGPIO0H_RES					(0x64)
#define AXP20_VGPIO0L_RES					(0x65)
#define AXP20_VGPIO1H_RES					(0x66)
#define AXP20_VGPIO1L_RES					(0x67)
#define AXP20_VGPIO2H_RES					(0x68)
#define AXP20_VGPIO2L_RES					(0x69)
#define AXP20_VGPIO3H_RES					(0x6A)
#define AXP20_VGPIO3L_RES					(0x6B)

#define AXP20_PBATH_RES						POWER20_BAT_POWERH8
#define AXP20_PBATM_RES						POWER20_BAT_POWERM8
#define AXP20_PBATL_RES						POWER20_BAT_POWERL8

#define AXP20_VBATH_RES						POWER20_BAT_AVERVOL_H8
#define AXP20_VBATL_RES						POWER20_BAT_AVERVOL_L4
#define AXP20_ICHARH_RES					POWER20_BAT_AVERCHGCUR_H8
#define AXP20_ICHARL_RES					POWER20_BAT_AVERCHGCUR_L5
#define AXP20_IDISCHARH_RES					POWER20_BAT_AVERDISCHGCUR_H8
#define AXP20_IDISCHARL_RES					POWER20_BAT_AVERDISCHGCUR_L5
#define AXP20_VAPSH_RES						POWER20_APS_AVERVOL_H8
#define AXP20_VAPSL_RES						POWER20_APS_AVERVOL_L4


#define AXP20_COULOMB_CONTROL				POWER20_COULOMB_CTL
#define AXP20_COULOMB_ENABLE				(1 << 7)
#define AXP20_COULOMB_SUSPEND				(1 << 6)
#define AXP20_COULOMB_CLEAR					(1 << 5)

#define AXP20_CCHAR3_RES					POWER20_BAT_CHGCOULOMB3
#define AXP20_CCHAR2_RES					POWER20_BAT_CHGCOULOMB2
#define AXP20_CCHAR1_RES					POWER20_BAT_CHGCOULOMB1
#define AXP20_CCHAR0_RES					POWER20_BAT_CHGCOULOMB0
#define AXP20_CDISCHAR3_RES					POWER20_BAT_DISCHGCOULOMB3
#define AXP20_CDISCHAR2_RES					POWER20_BAT_DISCHGCOULOMB2
#define AXP20_CDISCHAR1_RES					POWER20_BAT_DISCHGCOULOMB1
#define AXP20_CDISCHAR0_RES					POWER20_BAT_DISCHGCOULOMB0

#define AXP20_DATA_BUFFER0					POWER20_DATA_BUFFER1
#define AXP20_DATA_BUFFER1					POWER20_DATA_BUFFER2
#define AXP20_DATA_BUFFER2					POWER20_DATA_BUFFER3
#define AXP20_DATA_BUFFER3					POWER20_DATA_BUFFER4
#define AXP20_DATA_BUFFER4					POWER20_DATA_BUFFER5
#define AXP20_DATA_BUFFER5					POWER20_DATA_BUFFER6
#define AXP20_DATA_BUFFER6					POWER20_DATA_BUFFER7
#define AXP20_DATA_BUFFER7					POWER20_DATA_BUFFER8
#define AXP20_DATA_BUFFER8					POWER20_DATA_BUFFER9
#define AXP20_DATA_BUFFER9					POWER20_DATA_BUFFERA
#define AXP20_DATA_BUFFERA					POWER20_DATA_BUFFERB
#define AXP20_DATA_BUFFERB					POWER20_DATA_BUFFERC
#define AXP20_IC_TYPE								POWER20_IC_TYPE

#define AXP20_CAP									(0xB9)

#define AXP20_CHARGE_VBUS					POWER20_IPS_SET
#define AXP20_APS_WARNING1				    POWER20_APS_WARNING1
#define AXP20_APS_WARNING2				    POWER20_APS_WARNING2
#define AXP20_TIMER_CTL						POWER20_TIMER_CTL

#define AXP20_INTTEMP							(0x5E)

#define  AXP20_NOTIFIER_ON				   (AXP20_IRQ_USBIN | \
				        					AXP20_IRQ_USBRE | \
				       						AXP20_IRQ_ACIN  | \
				       						AXP20_IRQ_ACRE  | \
				       						AXP20_IRQ_BATIN | \
				       						AXP20_IRQ_BATRE | \
				       						AXP20_IRQ_CHAST	| \
				       						AXP20_IRQ_CHAOV)



#define AXP_CHG_ATTR(_name)					\
{									\
	.attr = { .name = #_name,.mode = 0644 },					\
	.show =  _name##_show,				\
	.store = _name##_store, \
}

struct axp_adc_res {//struct change
	uint16_t vbat_res;
	uint16_t ibat_res;
	uint16_t ichar_res;
	uint16_t idischar_res;
	uint16_t vac_res;
	uint16_t iac_res;
	uint16_t vusb_res;
	uint16_t iusb_res;
};

struct axp20_supply {
    struct aml_charger aml_charger;
	unsigned int interval;

    void (*led_control)(int flag);

	/* adc */
	struct axp_adc_res adc;
	/*power supply sysfs*/
	struct power_supply batt;
	struct power_supply	ac;
	struct power_supply	usb;
	struct power_supply bubatt;

	/*i2c device*/
	struct device *master;
	/*monitor*/
	struct delayed_work work;
	/*battery info*/
	struct power_supply_info *battery_info;
};

/*
 * export global axp_charger struct so this struct can be used by call back function.
 */
extern struct axp20_supply *g_axp20_supply;

/*
 * R/W operation:
 * @addr       : register addres you want to R/W
 * @buf        : buffer used to maintain R/W data
 * @start_addr : start register address for multiple R/W
 * @size       : R/W size for multiple
 * @val        : value write to register
 */
extern int axp20_reg_read  (int addr, uint8_t *buf);
extern int axp20_reg_reads (int start_addr, uint8_t *buf, int size);
extern int axp20_reg_write (int addr, uint8_t val);
extern int axp20_reg_writes(int start_addr, uint8_t *buf, int size);
extern int axp20_set_bits  (int addr, uint8_t bits, uint8_t mask);

extern int axp_set_charge_current(int chgcur);                          // set charge current, in uA, 0 to disable charger
extern int axp_set_usb_voltage_limit(int voltage);                      // set usb voltage limit, in mV
extern int axp_set_noe_delay(int delay);                                // set pmu_pwrnoe_time, in ms
extern int axp_set_pek_on_time(int time);                               // set pmu_pekon_time, in ms
extern int axp_set_pek_long_time(int time);                             // set pmu_peklong_time, in ms
extern int axp_set_pek_off_en(int en);                                  // set pmu_pekoff_en,
extern int axp_set_pwrok_delay(int delay);                              // set pmu_pwrok_time, in ms
extern int axp_set_pek_off_time(int time);                              // set pmu_pekoff_time, in ms
extern int axp_set_ot_power_off_en(int en);                             // set pmu_intotp_en, over temperature protect
extern int axp_set_poweroff_voltage(int voltage);                       // set pmu_pwroff_vol, in mV
extern int axp_charger_set_usbcur_limit_extern(int usbcur_limit);       // set usb current limit, in mA, 0 to disable limit

extern int axp_get_battery_percent(void);                               // return percent of battery capacity now
extern int axp_get_battery_voltage(void);                               // return battery voltage, in mV not OCV
extern int axp_get_battery_current(void);                               // return battery current, in mA
#endif      /* _LINUX_AXP_SPLY_H_ */

