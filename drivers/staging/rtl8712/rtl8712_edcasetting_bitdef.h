#ifndef __RTL8712_EDCASETTING_BITDEF_H__
#define __RTL8712_EDCASETTING_BITDEF_H__

/*EDCAPARAM*/
#define	_TXOPLIMIT_MSK		0xFFFF0000
#define	_TXOPLIMIT_SHT		16
#define	_ECWIN_MSK		0x0000FF00
#define	_ECWIN_SHT		8
#define	_AIFS_MSK		0x000000FF
#define	_AIFS_SHT		0

/*BCNTCFG*/
#define	_BCNECW_MSK		0xFF00
#define	_BCNECW_SHT		8
#define	_BCNIFS_MSK		0x00FF
#define	_BCNIFS_SHT		0

/*CWRR*/
#define	_CWRR_MSK		0x03FF

/*ACMAVG*/
#define	_AVG_TIME_UP		BIT(3)
#define	_AVGPERIOD_MSK		0x03

/*ACMHWCTRL*/
#define	_VOQ_ACM_STATUS		BIT(6)
#define	_VIQ_ACM_STATUS		BIT(5)
#define	_BEQ_ACM_STATUS		BIT(4)
#define	_VOQ_ACM_EN		BIT(3)
#define	_VIQ_ACM_EN		BIT(2)
#define	_BEQ_ACM_EN		BIT(1)
#define	_ACMHWEN		BIT(0)

/*VO_ADMTIME*/
#define	_VO_ACM_RUT		BIT(18)
#define	_VO_ADMTIME_MSK		0x0003FFF

/*VI_ADMTIME*/
#define	_VI_ACM_RUT		BIT(18)
#define	_VI_ADMTIME_MSK		0x0003FFF

/*BE_ADMTIME*/
#define	_BE_ACM_RUT		BIT(18)
#define	_BE_ADMTIME_MSK		0x0003FFF

/*Retry limit reg*/
#define	_SRL_MSK		0xFF00
#define	_SRL_SHT		8
#define	_LRL_MSK		0x00FF
#define	_LRL_SHT		0

#endif /* __RTL8712_EDCASETTING_BITDEF_H__*/
