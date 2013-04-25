/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#define RTMP_MODULE_OS

/*#include "rt_config.h"*/
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"

/* module table */
USB_DEVICE_ID rtusb_dev_id[] = {
#ifdef RT3070
	{USB_DEVICE(0x148F,0x3070)}, /* Ralink 3070 */
	{USB_DEVICE(0x148F,0x3071)}, /* Ralink 3071 */
	{USB_DEVICE(0x148F,0x3072)}, /* Ralink 3072 */
	{USB_DEVICE(0x0DB0,0x3820)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DB0,0x871C)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DB0,0x822C)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DB0,0x871B)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DB0,0x822B)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DF6,0x003E)}, /* Sitecom 3070 */
	{USB_DEVICE(0x0DF6,0x0042)}, /* Sitecom 3072 */
	{USB_DEVICE(0x0DF6,0x0048)}, /* Sitecom 3070 */
	{USB_DEVICE(0x0DF6,0x0047)}, /* Sitecom 3071 */
	{USB_DEVICE(0x0DF6,0x005F)}, /* Sitecom 3072 */
	{USB_DEVICE(0x14B2,0x3C12)}, /* AL 3070 */
	{USB_DEVICE(0x18C5,0x0012)}, /* Corega 3070 */
	{USB_DEVICE(0x083A,0x7511)}, /* Arcadyan 3070 */
	{USB_DEVICE(0x083A,0xA701)}, /* SMC 3070 */
	{USB_DEVICE(0x083A,0xA702)}, /* SMC 3072 */
	{USB_DEVICE(0x1740,0x9703)}, /* EnGenius 3070 */
	{USB_DEVICE(0x1740,0x9705)}, /* EnGenius 3071 */
	{USB_DEVICE(0x1740,0x9706)}, /* EnGenius 3072 */
	{USB_DEVICE(0x1740,0x9707)}, /* EnGenius 3070 */
	{USB_DEVICE(0x1740,0x9708)}, /* EnGenius 3071 */
	{USB_DEVICE(0x1740,0x9709)}, /* EnGenius 3072 */
	{USB_DEVICE(0x13D3,0x3273)}, /* AzureWave 3070*/
	{USB_DEVICE(0x13D3,0x3305)}, /* AzureWave 3070*/
	{USB_DEVICE(0x1044,0x800D)}, /* Gigabyte GN-WB32L 3070 */
	{USB_DEVICE(0x2019,0xAB25)}, /* Planex Communications, Inc. RT3070 */
	{USB_DEVICE(0x2019,0x5201)}, /* Planex Communications, Inc. RT8070 */
	{USB_DEVICE(0x07B8,0x3070)}, /* AboCom 3070 */
	{USB_DEVICE(0x07B8,0x3071)}, /* AboCom 3071 */
	{USB_DEVICE(0x07B8,0x3072)}, /* Abocom 3072 */
	{USB_DEVICE(0x7392,0x7711)}, /* Edimax 3070 */
	{USB_DEVICE(0x7392,0x4085)}, /* 2L Central Europe BV 8070 */
	{USB_DEVICE(0x1A32,0x0304)}, /* Quanta 3070 */
	{USB_DEVICE(0x1EDA,0x2310)}, /* AirTies 3070 */
	{USB_DEVICE(0x07D1,0x3C0A)}, /* D-Link 3072 */
	{USB_DEVICE(0x07D1,0x3C0D)}, /* D-Link 3070 */
	{USB_DEVICE(0x07D1,0x3C0E)}, /* D-Link 3070 */
	{USB_DEVICE(0x07D1,0x3C0F)}, /* D-Link 3070 */
	{USB_DEVICE(0x07D1,0x3C16)}, /* D-Link 3070 */
	{USB_DEVICE(0x07D1,0x3C17)}, /* D-Link 8070 */
	{USB_DEVICE(0x1D4D,0x000C)}, /* Pegatron Corporation 3070 */
	{USB_DEVICE(0x1D4D,0x000E)}, /* Pegatron Corporation 3070 */
	{USB_DEVICE(0x1D4D,0x0011)}, /* Pegatron Corporation 3072 */
	{USB_DEVICE(0x5A57,0x5257)}, /* Zinwell 3070 */
	{USB_DEVICE(0x5A57,0x0283)}, /* Zinwell 3072 */
	{USB_DEVICE(0x04BB,0x0945)}, /* I-O DATA 3072 */
	{USB_DEVICE(0x04BB,0x0947)}, /* I-O DATA 3070 */
	{USB_DEVICE(0x04BB,0x0948)}, /* I-O DATA 3072 */
	{USB_DEVICE(0x203D,0x1480)}, /* Encore 3070 */
	{USB_DEVICE(0x20B8,0x8888)}, /* PARA INDUSTRIAL 3070 */
	{USB_DEVICE(0x0B05,0x1784)}, /* Asus 3072 */
	{USB_DEVICE(0x203D,0x14A9)}, /* Encore 3070*/
	{USB_DEVICE(0x0DB0,0x899A)}, /* MSI 3070*/
	{USB_DEVICE(0x0DB0,0x3870)}, /* MSI 3070*/
	{USB_DEVICE(0x0DB0,0x870A)}, /* MSI 3070*/
	{USB_DEVICE(0x0DB0,0x6899)}, /* MSI 3070 */
	{USB_DEVICE(0x0DB0,0x3822)}, /* MSI 3070 */
	{USB_DEVICE(0x0DB0,0x3871)}, /* MSI 3070 */
	{USB_DEVICE(0x0DB0,0x871A)}, /* MSI 3070 */
	{USB_DEVICE(0x0DB0,0x822A)}, /* MSI 3070 */
	{USB_DEVICE(0x0DB0,0x3821)}, /* Ralink 3070 */
	{USB_DEVICE(0x0DB0,0x821A)}, /* Ralink 3070 */
	{USB_DEVICE(0x5A57,0x0282)}, /* zintech 3072 */	
	{USB_DEVICE(0x083A,0xA703)}, /* IO-MAGIC */
	{USB_DEVICE(0x13D3,0x3307)}, /* Azurewave */
	{USB_DEVICE(0x13D3,0x3321)}, /* Azurewave */
	{USB_DEVICE(0x07FA,0x7712)}, /* Edimax */
	{USB_DEVICE(0x0789,0x0166)}, /* Edimax */
	{USB_DEVICE(0x0586,0x341A)}, /* Zyxel */
	{USB_DEVICE(0x0586,0x341E)}, /* Zyxel */
	{USB_DEVICE(0x0586,0x343E)}, /* Zyxel */
	{USB_DEVICE(0x1EDA,0x2012)}, /* Airties */
#endif /* RT3070 */
#ifdef RT3370
	{USB_DEVICE(0x148F,0x3370)}, /* Ralink 3370 */
	{USB_DEVICE(0x0DF6,0x0050)}, /* Sitecom 3370 */
#endif /* RT3370*/
#ifdef RT5370
	{USB_DEVICE(0x148F,0x5370)}, /* Ralink 5370 */	
	{USB_DEVICE(0x148F,0x5372)}, /* Ralink 5370 */	
	{USB_DEVICE(0x13D3,0x3365)}, /* Azurewave */
	{USB_DEVICE(0x13D3,0x3329)}, /* Azurewave */
#endif // RT5370 //
	{ }/* Terminating entry */
};

INT const rtusb_usb_id_len = sizeof(rtusb_dev_id) / sizeof(USB_DEVICE_ID);

MODULE_DEVICE_TABLE(usb, rtusb_dev_id);
