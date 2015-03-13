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


#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT

#if defined(CONFIG_USB_HCI)

#ifndef MAX_RECVBUF_SZ
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
		        #define MAX_RECVBUF_SZ (15360) // 15k < 16k
		#endif
	#else
		#define MAX_RECVBUF_SZ (4000) // about 4K
	#endif
#endif
#endif //!MAX_RECVBUF_SZ

#elif defined(CONFIG_PCI_HCI)
//#ifndef CONFIG_MINIMAL_MEMORY_USAGE
//	#define MAX_RECVBUF_SZ (9100)
//#else
	#define MAX_RECVBUF_SZ (4000) // about 4K
//#endif

#endif

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

void rtl8192d_translate_rx_signal_stuff(union recv_frame *precvframe, struct phy_stat *pphy_status);
void rtl8192d_query_rx_desc_status(union recv_frame *precvframe, struct recv_stat *pdesc);

#endif

