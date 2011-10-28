#ifndef __RTL8712_WMAC_BITDEF_H__
#define __RTL8712_WMAC_BITDEF_H__

/*NAVCTRL*/
#define	_NAV_UPPER_EN			BIT(18)
#define	_NAV_MTO_EN				BIT(17)
#define	_NAV_UPPER				BIT(16)
#define	_NAV_MTO_MSK			0xFF00
#define	_NAV_MTO_SHT			8
#define	_RTSRST_MSK				0x00FF
#define	_RTSRST_SHT				0

/*BWOPMODE*/
#define	_20MHZBW				BIT(2)

/*BACAMCMD*/
#define	_BACAM_POLL				BIT(31)
#define	_BACAM_RST				BIT(17)
#define	_BACAM_RW				BIT(16)
#define	_BACAM_ADDR_MSK			0x0000007F
#define	_BACAM_ADDR_SHT			0

/*LBDLY*/
#define	_LBDLY_MSK				0x1F

/*FWDLY*/
#define	_FWDLY_MSK				0x0F

/*RXERR_RPT*/
#define	_RXERR_RPT_SEL_MSK		0xF0000000
#define	_RXERR_RPT_SEL_SHT		28
#define	_RPT_CNT_MSK			0x000FFFFF
#define	_RPT_CNT_SHT			0


#endif	/*__RTL8712_WMAC_BITDEF_H__*/

