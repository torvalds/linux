/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _RTL8192D_RECV_H_
#define _RTL8192D_RECV_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifdef PLATFORM_OS_XP
	#ifdef CONFIG_SDIO_HCI
		#define NR_RECVBUFF 1024//512//128
	#else
		#define NR_RECVBUFF (16)
	#endif
#elif defined(PLATFORM_OS_CE)
	#ifdef CONFIG_SDIO_HCI
		#define NR_RECVBUFF (128)
	#else
		#define NR_RECVBUFF (4)
	#endif
#else
#ifdef CONFIG_SINGLE_RECV_BUF
	#define NR_RECVBUFF (1)
#else
	#define NR_RECVBUFF (4)
#endif //CONFIG_SINGLE_RECV_BUF
	#define NR_PREALLOC_RECV_SKB (8)
#endif



#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT

#if defined(CONFIG_USB_HCI)

#ifdef PLATFORM_OS_CE
#define MAX_RECVBUF_SZ (8192+1024) // 8K+1k
#else
	#ifndef CONFIG_MINIMAL_MEMORY_USAGE
		//#define MAX_RECVBUF_SZ (32768) // 32k
		//#define MAX_RECVBUF_SZ (16384) //16K
		//#define MAX_RECVBUF_SZ (10240) //10K
		#ifdef CONFIG_PLATFORM_MSTAR
			#define MAX_RECVBUF_SZ (8192) // 8K
		#else
			#ifndef CONFIG_USB_RX_AGGREGATION
			#define MAX_RECVBUF_SZ (4000) // about 4K
			#else
			#define MAX_RECVBUF_SZ (15360) // 15k < 16k
			#endif //CONFIG_USB_RX_AGGREGATION
		#endif
	#else
		#define MAX_RECVBUF_SZ (4000) // about 4K
	#endif
#endif

#elif defined(CONFIG_PCI_HCI)
//#ifndef CONFIG_MINIMAL_MEMORY_USAGE
//	#define MAX_RECVBUF_SZ (9100)
//#else
	#define MAX_RECVBUF_SZ (4000) // about 4K
//#endif

#define RX_MPDU_QUEUE				0
#define RX_CMD_QUEUE				1
#define RX_MAX_QUEUE				2
#endif

#define RECV_BULK_IN_ADDR		0x80
#define RECV_INT_IN_ADDR		0x81

#define PHY_RSSI_SLID_WIN_MAX				100
#define PHY_LINKQUALITY_SLID_WIN_MAX		20

struct phy_stat
{
	unsigned int phydw0;

	unsigned int phydw1;

	unsigned int phydw2;

	unsigned int phydw3;

	unsigned int phydw4;

	unsigned int phydw5;

	unsigned int phydw6;

	unsigned int phydw7;
};

typedef struct _Phy_OFDM_Rx_Status_Report_8192cd
{
	unsigned char	trsw_gain_X[4];
	unsigned char	pwdb_all;
	unsigned char	cfosho_X[4];
	unsigned char	cfotail_X[4];
	unsigned char	rxevm_X[2];
	unsigned char	rxsnr_X[4];
	unsigned char	pdsnr_X[2];
	unsigned char	csi_current_X[2];
	unsigned char	csi_target_X[2];
	unsigned char	sigevm;
	unsigned char	max_ex_pwr;
//#ifdef RTL8192SE
#ifdef CONFIG_LITTLE_ENDIAN
	unsigned char ex_intf_flg:1;
	unsigned char sgi_en:1;
	unsigned char rxsc:2;
	//unsigned char rsvd:4;
	unsigned char idle_long:1;
	unsigned char r_ant_train_en:1;
	unsigned char ANTSELB:1;
	unsigned char ANTSEL:1;	
#else	// _BIG_ENDIAN_
	//unsigned char rsvd:4;
	unsigned char ANTSEL:1;	
	unsigned char ANTSELB:1;
	unsigned char r_ant_train_en:1;
	unsigned char idle_long:1;
	unsigned char rxsc:2;
	unsigned char sgi_en:1;
	unsigned char ex_intf_flg:1;
#endif
//#else	// RTL8190, RTL8192E
//	unsigned char	sgi_en;
//	unsigned char	rxsc_sgien_exflg;
//#endif
}__attribute__ ((packed)) PHY_STS_OFDM_8192CD_T,PHY_RX_DRIVER_INFO_8192CD;

typedef struct _Phy_CCK_Rx_Status_Report_8192cd
{
	/* For CCK rate descriptor. This is a signed 8:1 variable. LSB bit presend
		0.5. And MSB 7 bts presend a signed value. Range from -64~+63.5. */
	u8	adc_pwdb_X[4];
	u8	SQ_rpt;
	u8	cck_agc_rpt;
} PHY_STS_CCK_8192CD_T;

// Rx smooth factor
#define	Rx_Smooth_Factor (20)

#ifdef CONFIG_USB_HCI
typedef struct _INTERRUPT_MSG_FORMAT_EX{
	unsigned int C2H_MSG0;
	unsigned int C2H_MSG1;
	unsigned int C2H_MSG2;
	unsigned int C2H_MSG3;
	unsigned int HISR; // from HISR Reg0x124, read to clear
	unsigned int HISRE;// from HISRE Reg0x12c, read to clear
	unsigned int  MSG_EX;
}INTERRUPT_MSG_FORMAT_EX,*PINTERRUPT_MSG_FORMAT_EX;

void rtl8192du_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
int	rtl8192du_init_recv_priv(_adapter * padapter);
void	rtl8192du_free_recv_priv(_adapter * padapter);
#endif

#ifdef CONFIG_PCI_HCI
int	rtl8192de_init_recv_priv(_adapter * padapter);
void rtl8192de_free_recv_priv(_adapter * padapter);
#endif

void rtl8192d_translate_rx_signal_stuff(union recv_frame *precvframe, struct phy_stat *pphy_info);
void rtl8192d_query_rx_desc_status(union recv_frame *precvframe, struct recv_stat *pdesc);

#endif

