/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: applicom.h,v 1.2 1999/08/28 15:09:49 dwmw2 Exp $ */


#ifndef __LINUX_APPLICOM_H__
#define __LINUX_APPLICOM_H__


#define DATA_TO_PC_READY      0x00
#define TIC_OWNER_TO_PC       0x01
#define NUMCARD_OWNER_TO_PC   0x02
#define TIC_DES_TO_PC         0x03
#define NUMCARD_DES_TO_PC     0x04
#define DATA_FROM_PC_READY    0x05
#define TIC_OWNER_FROM_PC     0x06
#define NUMCARD_OWNER_FROM_PC 0x07
#define TIC_DES_FROM_PC       0x08
#define NUMCARD_DES_FROM_PC   0x09
#define ACK_FROM_PC_READY     0x0E
#define TIC_ACK_FROM_PC       0x0F
#define NUMCARD_ACK_FROM_PC   0x010
#define TYP_ACK_FROM_PC       0x011
#define CONF_END_TEST         0x012
#define ERROR_CODE            0x016 
#define PARAMETER_ERROR       0x018 
#define VERS                  0x01E 
#define RAM_TO_PC             0x040
#define RAM_FROM_PC           0x0170
#define TYPE_CARD             0x03C0
#define SERIAL_NUMBER         0x03DA
#define RAM_IT_FROM_PC        0x03FE
#define RAM_IT_TO_PC          0x03FF

struct mailbox{
	u16  stjb_codef;		/* offset 00 */
	s16  stjb_status;     		/* offset 02 */
	u16  stjb_ticuser_root;		/* offset 04 */
	u8   stjb_piduser[4];		/* offset 06 */
	u16  stjb_mode;			/* offset 0A */
	u16  stjb_time;			/* offset 0C */
	u16  stjb_stop;			/* offset 0E */
	u16  stjb_nfonc;		/* offset 10 */
	u16  stjb_ncard;		/* offset 12 */
	u16  stjb_nchan;		/* offset 14 */
	u16  stjb_nes;			/* offset 16 */
	u16  stjb_nb;			/* offset 18 */
	u16  stjb_typvar;		/* offset 1A */
	u32  stjb_adr;			/* offset 1C */
	u16  stjb_ticuser_dispcyc;	/* offset 20 */
	u16  stjb_ticuser_protocol;	/* offset 22 */
	u8   stjb_filler[12];		/* offset 24 */
	u8   stjb_data[256];		/* offset 30 */
	};

struct st_ram_io 
{
	unsigned char data_to_pc_ready;
	unsigned char tic_owner_to_pc;
	unsigned char numcard_owner_to_pc;
	unsigned char tic_des_to_pc;
	unsigned char numcard_des_to_pc;
	unsigned char data_from_pc_ready;
	unsigned char tic_owner_from_pc;
	unsigned char numcard_owner_from_pc;
	unsigned char tic_des_from_pc;
	unsigned char numcard_des_from_pc;
	unsigned char ack_to_pc_ready;
	unsigned char tic_ack_to_pc;
	unsigned char numcard_ack_to_pc;
	unsigned char typ_ack_to_pc;
	unsigned char ack_from_pc_ready;
	unsigned char tic_ack_from_pc;
	unsigned char numcard_ack_from_pc;
	unsigned char typ_ack_from_pc;
	unsigned char conf_end_test[4];
	unsigned char error_code[2];
	unsigned char parameter_error[4];
	unsigned char time_base;
	unsigned char nul_inc;
	unsigned char vers;
	unsigned char num_card;
	unsigned char reserv1[32];
};


#endif /* __LINUX_APPLICOM_H__ */
