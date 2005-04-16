/*
 *  linux/drivers/s390/crypto/z90hardware.c
 *
 *  z90crypt 1.3.2
 *
 *  Copyright (C)  2001, 2004 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/uaccess.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include "z90crypt.h"
#include "z90common.h"

#define VERSION_Z90HARDWARE_C "$Revision: 1.33 $"

char z90hardware_version[] __initdata =
	"z90hardware.o (" VERSION_Z90HARDWARE_C "/"
	                  VERSION_Z90COMMON_H "/" VERSION_Z90CRYPT_H ")";

struct cca_token_hdr {
	unsigned char  token_identifier;
	unsigned char  version;
	unsigned short token_length;
	unsigned char  reserved[4];
};

#define CCA_TKN_HDR_ID_EXT 0x1E

struct cca_private_ext_ME_sec {
	unsigned char  section_identifier;
	unsigned char  version;
	unsigned short section_length;
	unsigned char  private_key_hash[20];
	unsigned char  reserved1[4];
	unsigned char  key_format;
	unsigned char  reserved2;
	unsigned char  key_name_hash[20];
	unsigned char  key_use_flags[4];
	unsigned char  reserved3[6];
	unsigned char  reserved4[24];
	unsigned char  confounder[24];
	unsigned char  exponent[128];
	unsigned char  modulus[128];
};

#define CCA_PVT_USAGE_ALL 0x80

struct cca_public_sec {
	unsigned char  section_identifier;
	unsigned char  version;
	unsigned short section_length;
	unsigned char  reserved[2];
	unsigned short exponent_len;
	unsigned short modulus_bit_len;
	unsigned short modulus_byte_len;
	unsigned char  exponent[3];
};

struct cca_private_ext_ME {
	struct cca_token_hdr	      pvtMEHdr;
	struct cca_private_ext_ME_sec pvtMESec;
	struct cca_public_sec	      pubMESec;
};

struct cca_public_key {
	struct cca_token_hdr  pubHdr;
	struct cca_public_sec pubSec;
};

struct cca_pvt_ext_CRT_sec {
	unsigned char  section_identifier;
	unsigned char  version;
	unsigned short section_length;
	unsigned char  private_key_hash[20];
	unsigned char  reserved1[4];
	unsigned char  key_format;
	unsigned char  reserved2;
	unsigned char  key_name_hash[20];
	unsigned char  key_use_flags[4];
	unsigned short p_len;
	unsigned short q_len;
	unsigned short dp_len;
	unsigned short dq_len;
	unsigned short u_len;
	unsigned short mod_len;
	unsigned char  reserved3[4];
	unsigned short pad_len;
	unsigned char  reserved4[52];
	unsigned char  confounder[8];
};

#define CCA_PVT_EXT_CRT_SEC_ID_PVT 0x08
#define CCA_PVT_EXT_CRT_SEC_FMT_CL 0x40

struct cca_private_ext_CRT {
	struct cca_token_hdr	   pvtCrtHdr;
	struct cca_pvt_ext_CRT_sec pvtCrtSec;
	struct cca_public_sec	   pubCrtSec;
};

struct ap_status_word {
	unsigned char q_stat_flags;
	unsigned char response_code;
	unsigned char reserved[2];
};

#define AP_Q_STATUS_EMPTY		0x80
#define AP_Q_STATUS_REPLIES_WAITING	0x40
#define AP_Q_STATUS_ARRAY_FULL		0x20

#define AP_RESPONSE_NORMAL		0x00
#define AP_RESPONSE_Q_NOT_AVAIL		0x01
#define AP_RESPONSE_RESET_IN_PROGRESS	0x02
#define AP_RESPONSE_DECONFIGURED	0x03
#define AP_RESPONSE_CHECKSTOPPED	0x04
#define AP_RESPONSE_BUSY		0x05
#define AP_RESPONSE_Q_FULL		0x10
#define AP_RESPONSE_NO_PENDING_REPLY	0x10
#define AP_RESPONSE_INDEX_TOO_BIG	0x11
#define AP_RESPONSE_NO_FIRST_PART	0x13
#define AP_RESPONSE_MESSAGE_TOO_BIG	0x15

#define AP_MAX_CDX_BITL		4
#define AP_RQID_RESERVED_BITL	4
#define SKIP_BITL		(AP_MAX_CDX_BITL + AP_RQID_RESERVED_BITL)

struct type4_hdr {
	unsigned char  reserved1;
	unsigned char  msg_type_code;
	unsigned short msg_len;
	unsigned char  request_code;
	unsigned char  msg_fmt;
	unsigned short reserved2;
};

#define TYPE4_TYPE_CODE 0x04
#define TYPE4_REQU_CODE 0x40

#define TYPE4_SME_LEN 0x0188
#define TYPE4_LME_LEN 0x0308
#define TYPE4_SCR_LEN 0x01E0
#define TYPE4_LCR_LEN 0x03A0

#define TYPE4_SME_FMT 0x00
#define TYPE4_LME_FMT 0x10
#define TYPE4_SCR_FMT 0x40
#define TYPE4_LCR_FMT 0x50

struct type4_sme {
	struct type4_hdr header;
	unsigned char	 message[128];
	unsigned char	 exponent[128];
	unsigned char	 modulus[128];
};

struct type4_lme {
	struct type4_hdr header;
	unsigned char	 message[256];
	unsigned char	 exponent[256];
	unsigned char	 modulus[256];
};

struct type4_scr {
	struct type4_hdr header;
	unsigned char	 message[128];
	unsigned char	 dp[72];
	unsigned char	 dq[64];
	unsigned char	 p[72];
	unsigned char	 q[64];
	unsigned char	 u[72];
};

struct type4_lcr {
	struct type4_hdr header;
	unsigned char	 message[256];
	unsigned char	 dp[136];
	unsigned char	 dq[128];
	unsigned char	 p[136];
	unsigned char	 q[128];
	unsigned char	 u[136];
};

union type4_msg {
	struct type4_sme sme;
	struct type4_lme lme;
	struct type4_scr scr;
	struct type4_lcr lcr;
};

struct type84_hdr {
	unsigned char  reserved1;
	unsigned char  code;
	unsigned short len;
	unsigned char  reserved2[4];
};

#define TYPE84_RSP_CODE 0x84

struct type6_hdr {
	unsigned char reserved1;
	unsigned char type;
	unsigned char reserved2[2];
	unsigned char right[4];
	unsigned char reserved3[2];
	unsigned char reserved4[2];
	unsigned char apfs[4];
	unsigned int  offset1;
	unsigned int  offset2;
	unsigned int  offset3;
	unsigned int  offset4;
	unsigned char agent_id[16];
	unsigned char rqid[2];
	unsigned char reserved5[2];
	unsigned char function_code[2];
	unsigned char reserved6[2];
	unsigned int  ToCardLen1;
	unsigned int  ToCardLen2;
	unsigned int  ToCardLen3;
	unsigned int  ToCardLen4;
	unsigned int  FromCardLen1;
	unsigned int  FromCardLen2;
	unsigned int  FromCardLen3;
	unsigned int  FromCardLen4;
};

struct CPRB {
	unsigned char cprb_len[2];
	unsigned char cprb_ver_id;
	unsigned char pad_000;
	unsigned char srpi_rtcode[4];
	unsigned char srpi_verb;
	unsigned char flags;
	unsigned char func_id[2];
	unsigned char checkpoint_flag;
	unsigned char resv2;
	unsigned char req_parml[2];
	unsigned char req_parmp[4];
	unsigned char req_datal[4];
	unsigned char req_datap[4];
	unsigned char rpl_parml[2];
	unsigned char pad_001[2];
	unsigned char rpl_parmp[4];
	unsigned char rpl_datal[4];
	unsigned char rpl_datap[4];
	unsigned char ccp_rscode[2];
	unsigned char ccp_rtcode[2];
	unsigned char repd_parml[2];
	unsigned char mac_data_len[2];
	unsigned char repd_datal[4];
	unsigned char req_pc[2];
	unsigned char res_origin[8];
	unsigned char mac_value[8];
	unsigned char logon_id[8];
	unsigned char usage_domain[2];
	unsigned char resv3[18];
	unsigned char svr_namel[2];
	unsigned char svr_name[8];
};

struct type6_msg {
	struct type6_hdr header;
	struct CPRB	 CPRB;
};

union request_msg {
	union  type4_msg t4msg;
	struct type6_msg t6msg;
};

struct request_msg_ext {
	int		  q_nr;
	unsigned char	  *psmid;
	union request_msg reqMsg;
};

struct type82_hdr {
	unsigned char reserved1;
	unsigned char type;
	unsigned char reserved2[2];
	unsigned char reply_code;
	unsigned char reserved3[3];
};

#define TYPE82_RSP_CODE 0x82

#define REPLY_ERROR_MACHINE_FAILURE  0x10
#define REPLY_ERROR_PREEMPT_FAILURE  0x12
#define REPLY_ERROR_CHECKPT_FAILURE  0x14
#define REPLY_ERROR_MESSAGE_TYPE     0x20
#define REPLY_ERROR_INVALID_COMM_CD  0x21
#define REPLY_ERROR_INVALID_MSG_LEN  0x23
#define REPLY_ERROR_RESERVD_FIELD    0x24
#define REPLY_ERROR_FORMAT_FIELD     0x29
#define REPLY_ERROR_INVALID_COMMAND  0x30
#define REPLY_ERROR_MALFORMED_MSG    0x40
#define REPLY_ERROR_RESERVED_FIELDO  0x50
#define REPLY_ERROR_WORD_ALIGNMENT   0x60
#define REPLY_ERROR_MESSAGE_LENGTH   0x80
#define REPLY_ERROR_OPERAND_INVALID  0x82
#define REPLY_ERROR_OPERAND_SIZE     0x84
#define REPLY_ERROR_EVEN_MOD_IN_OPND 0x85
#define REPLY_ERROR_RESERVED_FIELD   0x88
#define REPLY_ERROR_TRANSPORT_FAIL   0x90
#define REPLY_ERROR_PACKET_TRUNCATED 0xA0
#define REPLY_ERROR_ZERO_BUFFER_LEN  0xB0

struct type86_hdr {
	unsigned char reserved1;
	unsigned char type;
	unsigned char format;
	unsigned char reserved2;
	unsigned char reply_code;
	unsigned char reserved3[3];
};

#define TYPE86_RSP_CODE 0x86
#define TYPE86_FMT2	0x02

struct type86_fmt2_msg {
	struct type86_hdr hdr;
	unsigned char	  reserved[4];
	unsigned char	  apfs[4];
	unsigned int	  count1;
	unsigned int	  offset1;
	unsigned int	  count2;
	unsigned int	  offset2;
	unsigned int	  count3;
	unsigned int	  offset3;
	unsigned int	  count4;
	unsigned int	  offset4;
};

static struct type6_hdr static_type6_hdr = {
	0x00,
	0x06,
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	0x00000058,
	0x00000000,
	0x00000000,
	0x00000000,
	{0x01,0x00,0x43,0x43,0x41,0x2D,0x41,0x50,
	 0x50,0x4C,0x20,0x20,0x20,0x01,0x01,0x01},
	{0x00,0x00},
	{0x00,0x00},
	{0x50,0x44},
	{0x00,0x00},
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

static struct type6_hdr static_type6_hdrX = {
	0x00,
	0x06,
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	0x00000058,
	0x00000000,
	0x00000000,
	0x00000000,
	{0x43,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x50,0x44},
	{0x00,0x00},
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

static struct CPRB static_cprb = {
	{0x70,0x00},
	0x41,
	0x00,
	{0x00,0x00,0x00,0x00},
	0x00,
	0x00,
	{0x54,0x32},
	0x01,
	0x00,
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00},
	{0x08,0x00},
	{0x49,0x43,0x53,0x46,0x20,0x20,0x20,0x20}
};

struct function_and_rules_block {
	unsigned char function_code[2];
	unsigned char ulen[2];
	unsigned char only_rule[8];
};

static struct function_and_rules_block static_pkd_function_and_rules = {
	{0x50,0x44},
	{0x0A,0x00},
	{'P','K','C','S','-','1','.','2'}
};

static struct function_and_rules_block static_pke_function_and_rules = {
	{0x50,0x4B},
	{0x0A,0x00},
	{'P','K','C','S','-','1','.','2'}
};

struct T6_keyBlock_hdr {
	unsigned char blen[2];
	unsigned char ulen[2];
	unsigned char flags[2];
};

static struct T6_keyBlock_hdr static_T6_keyBlock_hdr = {
	{0x89,0x01},
	{0x87,0x01},
	{0x00}
};

static struct CPRBX static_cprbx = {
	0x00DC,
	0x02,
	{0x00,0x00,0x00},
	{0x54,0x32},
	{0x00,0x00,0x00,0x00},
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	{0x00,0x00,0x00,0x00},
	0x00000000,
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	0x0000,
	0x0000,
	0x00000000,
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	0x00,
	0x00,
	0x0000,
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static struct function_and_rules_block static_pkd_function_and_rulesX_MCL2 = {
	{0x50,0x44},
	{0x00,0x0A},
	{'P','K','C','S','-','1','.','2'}
};

static struct function_and_rules_block static_pke_function_and_rulesX_MCL2 = {
	{0x50,0x4B},
	{0x00,0x0A},
	{'Z','E','R','O','-','P','A','D'}
};

static struct function_and_rules_block static_pkd_function_and_rulesX = {
	{0x50,0x44},
	{0x00,0x0A},
	{'Z','E','R','O','-','P','A','D'}
};

static struct function_and_rules_block static_pke_function_and_rulesX = {
	{0x50,0x4B},
	{0x00,0x0A},
	{'M','R','P',' ',' ',' ',' ',' '}
};

struct T6_keyBlock_hdrX {
	unsigned short blen;
	unsigned short ulen;
	unsigned char flags[2];
};

static unsigned char static_pad[256] = {
0x1B,0x7B,0x5D,0xB5,0x75,0x01,0x3D,0xFD,0x8D,0xD1,0xC7,0x03,0x2D,0x09,0x23,0x57,
0x89,0x49,0xB9,0x3F,0xBB,0x99,0x41,0x5B,0x75,0x21,0x7B,0x9D,0x3B,0x6B,0x51,0x39,
0xBB,0x0D,0x35,0xB9,0x89,0x0F,0x93,0xA5,0x0B,0x47,0xF1,0xD3,0xBB,0xCB,0xF1,0x9D,
0x23,0x73,0x71,0xFF,0xF3,0xF5,0x45,0xFB,0x61,0x29,0x23,0xFD,0xF1,0x29,0x3F,0x7F,
0x17,0xB7,0x1B,0xA9,0x19,0xBD,0x57,0xA9,0xD7,0x95,0xA3,0xCB,0xED,0x1D,0xDB,0x45,
0x7D,0x11,0xD1,0x51,0x1B,0xED,0x71,0xE9,0xB1,0xD1,0xAB,0xAB,0x21,0x2B,0x1B,0x9F,
0x3B,0x9F,0xF7,0xF7,0xBD,0x63,0xEB,0xAD,0xDF,0xB3,0x6F,0x5B,0xDB,0x8D,0xA9,0x5D,
0xE3,0x7D,0x77,0x49,0x47,0xF5,0xA7,0xFD,0xAB,0x2F,0x27,0x35,0x77,0xD3,0x49,0xC9,
0x09,0xEB,0xB1,0xF9,0xBF,0x4B,0xCB,0x2B,0xEB,0xEB,0x05,0xFF,0x7D,0xC7,0x91,0x8B,
0x09,0x83,0xB9,0xB9,0x69,0x33,0x39,0x6B,0x79,0x75,0x19,0xBF,0xBB,0x07,0x1D,0xBD,
0x29,0xBF,0x39,0x95,0x93,0x1D,0x35,0xC7,0xC9,0x4D,0xE5,0x97,0x0B,0x43,0x9B,0xF1,
0x16,0x93,0x03,0x1F,0xA5,0xFB,0xDB,0xF3,0x27,0x4F,0x27,0x61,0x05,0x1F,0xB9,0x23,
0x2F,0xC3,0x81,0xA9,0x23,0x71,0x55,0x55,0xEB,0xED,0x41,0xE5,0xF3,0x11,0xF1,0x43,
0x69,0x03,0xBD,0x0B,0x37,0x0F,0x51,0x8F,0x0B,0xB5,0x89,0x5B,0x67,0xA9,0xD9,0x4F,
0x01,0xF9,0x21,0x77,0x37,0x73,0x79,0xC5,0x7F,0x51,0xC1,0xCF,0x97,0xA1,0x75,0xAD,
0x35,0x9D,0xD3,0xD3,0xA7,0x9D,0x5D,0x41,0x6F,0x65,0x1B,0xCF,0xA9,0x87,0x91,0x09
};

static struct cca_private_ext_ME static_pvt_me_key = {
	{
		0x1E,
		0x00,
		0x0183,
		{0x00,0x00,0x00,0x00}
	},

	{
		0x02,
		0x00,
		0x016C,
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00},
		0x00,
		0x00,
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00},
		{0x80,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
	},

	{
		0x04,
		0x00,
		0x000F,
		{0x00,0x00},
		0x0003,
		0x0000,
		0x0000,
		{0x01,0x00,0x01}
	}
};

static struct cca_public_key static_public_key = {
	{
		0x1E,
		0x00,
		0x0000,
		{0x00,0x00,0x00,0x00}
	},

	{
		0x04,
		0x00,
		0x0000,
		{0x00,0x00},
		0x0000,
		0x0000,
		0x0000,
		{0x01,0x00,0x01}
	}
};

#define FIXED_TYPE6_ME_LEN 0x0000025F

#define FIXED_TYPE6_ME_EN_LEN 0x000000F0

#define FIXED_TYPE6_ME_LENX 0x000002CB

#define FIXED_TYPE6_ME_EN_LENX 0x0000015C

static struct cca_public_sec static_cca_pub_sec = {
	0x04,
	0x00,
	0x000f,
	{0x00,0x00},
	0x0003,
	0x0000,
	0x0000,
	{0x01,0x00,0x01}
};

#define FIXED_TYPE6_CR_LEN 0x00000177

#define FIXED_TYPE6_CR_LENX 0x000001E3

#define MAX_RESPONSE_SIZE 0x00000710

#define MAX_RESPONSEX_SIZE 0x0000077C

#define RESPONSE_CPRB_SIZE  0x000006B8
#define RESPONSE_CPRBX_SIZE 0x00000724

#define CALLER_HEADER 12

static unsigned char static_PKE_function_code[2] = {0x50, 0x4B};

static inline int
testq(int q_nr, int *q_depth, int *dev_type, struct ap_status_word *stat)
{
	int ccode;

	asm volatile
#ifdef __s390x__
	("	llgfr	0,%4		\n"
	 "	slgr	1,1		\n"
	 "	lgr	2,1		\n"
	 "0:	.long	0xb2af0000	\n"
	 "1:	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	iihh	%0,0		\n"
	 "	iihl	%0,0		\n"
	 "	lgr	%1,1		\n"
	 "	lgr	%3,2		\n"
	 "	srl	%3,24		\n"
	 "	sll	2,24		\n"
	 "	srl	2,24		\n"
	 "	lgr	%2,2		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h5		\n"
	 "	jg	2b		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	8		\n"
	 "	.quad	0b,3b		\n"
	 "	.quad	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat),"=d" (*q_depth), "=d" (*dev_type)
	 :"d" (q_nr), "K" (DEV_TSQ_EXCEPTION)
	 :"cc","0","1","2","memory");
#else
	("	lr	0,%4		\n"
	 "	slr	1,1		\n"
	 "	lr	2,1		\n"
	 "0:	.long	0xb2af0000	\n"
	 "1:	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	lr	%1,1		\n"
	 "	lr	%3,2		\n"
	 "	srl	%3,24		\n"
	 "	sll	2,24		\n"
	 "	srl	2,24		\n"
	 "	lr	%2,2		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h5		\n"
	 "	bras	1,4f		\n"
	 "	.long	2b		\n"
	 "4:				\n"
	 "	l	1,0(1)		\n"
	 "	br	1		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	4		\n"
	 "	.long	0b,3b		\n"
	 "	.long	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat),"=d" (*q_depth), "=d" (*dev_type)
	 :"d" (q_nr), "K" (DEV_TSQ_EXCEPTION)
	 :"cc","0","1","2","memory");
#endif
	return ccode;
}

static inline int
resetq(int q_nr, struct ap_status_word *stat_p)
{
	int ccode;

	asm volatile
#ifdef __s390x__
	("	llgfr	0,%2		\n"
	 "	lghi	1,1		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	slgr	1,1		\n"
	 "	lgr	2,1		\n"
	 "0:	.long	0xb2af0000	\n"
	 "1:	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	iihh	%0,0		\n"
	 "	iihl	%0,0		\n"
	 "	lgr	%1,1		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h3		\n"
	 "	jg	2b		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	8		\n"
	 "	.quad	0b,3b		\n"
	 "	.quad	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat_p)
	 :"d" (q_nr), "K" (DEV_RSQ_EXCEPTION)
	 :"cc","0","1","2","memory");
#else
	("	lr	0,%2		\n"
	 "	lhi	1,1		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	slr	1,1		\n"
	 "	lr	2,1		\n"
	 "0:	.long	0xb2af0000	\n"
	 "1:	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	lr	%1,1		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h3		\n"
	 "	bras	1,4f		\n"
	 "	.long	2b		\n"
	 "4:				\n"
	 "	l	1,0(1)		\n"
	 "	br	1		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	4		\n"
	 "	.long	0b,3b		\n"
	 "	.long	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat_p)
	 :"d" (q_nr), "K" (DEV_RSQ_EXCEPTION)
	 :"cc","0","1","2","memory");
#endif
	return ccode;
}

static inline int
sen(int msg_len, unsigned char *msg_ext, struct ap_status_word *stat)
{
	int ccode;

	asm volatile
#ifdef __s390x__
	("	lgr	6,%3		\n"
	 "	llgfr	7,%2		\n"
	 "	llgt	0,0(6)		\n"
	 "	lghi	1,64		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	la	6,4(6)		\n"
	 "	llgt	2,0(6)		\n"
	 "	llgt	3,4(6)		\n"
	 "	la	6,8(6)		\n"
	 "	slr	1,1		\n"
	 "0:	.long	0xb2ad0026	\n"
	 "1:	brc	2,0b		\n"
	 "	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	iihh	%0,0		\n"
	 "	iihl	%0,0		\n"
	 "	lgr	%1,1		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h4		\n"
	 "	jg	2b		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	8		\n"
	 "	.quad	0b,3b		\n"
	 "	.quad	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat)
	 :"d" (msg_len),"a" (msg_ext), "K" (DEV_SEN_EXCEPTION)
	 :"cc","0","1","2","3","6","7","memory");
#else
	("	lr	6,%3		\n"
	 "	lr	7,%2		\n"
	 "	l	0,0(6)		\n"
	 "	lhi	1,64		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	la	6,4(6)		\n"
	 "	l	2,0(6)		\n"
	 "	l	3,4(6)		\n"
	 "	la	6,8(6)		\n"
	 "	slr	1,1		\n"
	 "0:	.long	0xb2ad0026	\n"
	 "1:	brc	2,0b		\n"
	 "	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	lr	%1,1		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi	%0,%h4		\n"
	 "	bras	1,4f		\n"
	 "	.long	2b		\n"
	 "4:				\n"
	 "	l	1,0(1)		\n"
	 "	br	1		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "	.align	4		\n"
	 "	.long	0b,3b		\n"
	 "	.long	1b,3b		\n"
	 ".previous"
	 :"=d" (ccode),"=d" (*stat)
	 :"d" (msg_len),"a" (msg_ext), "K" (DEV_SEN_EXCEPTION)
	 :"cc","0","1","2","3","6","7","memory");
#endif
	return ccode;
}

static inline int
rec(int q_nr, int buff_l, unsigned char *rsp, unsigned char *id,
    struct ap_status_word *st)
{
	int ccode;

	asm volatile
#ifdef __s390x__
	("	llgfr	0,%2		\n"
	 "	lgr	3,%4		\n"
	 "	lgr	6,%3		\n"
	 "	llgfr	7,%5		\n"
	 "	lghi	1,128		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	slgr	1,1		\n"
	 "	lgr	2,1		\n"
	 "	lgr	4,1		\n"
	 "	lgr	5,1		\n"
	 "0:	.long	0xb2ae0046	\n"
	 "1:	brc	2,0b		\n"
	 "	brc	4,0b		\n"
	 "	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	iihh	%0,0		\n"
	 "	iihl	%0,0		\n"
	 "	lgr	%1,1		\n"
	 "	st	4,0(3)		\n"
	 "	st	5,4(3)		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi   %0,%h6		\n"
	 "	jg    2b		\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "   .align	8		\n"
	 "   .quad	0b,3b		\n"
	 "   .quad	1b,3b		\n"
	 ".previous"
	 :"=d"(ccode),"=d"(*st)
	 :"d" (q_nr), "d" (rsp), "d" (id), "d" (buff_l), "K" (DEV_REC_EXCEPTION)
	 :"cc","0","1","2","3","4","5","6","7","memory");
#else
	("	lr	0,%2		\n"
	 "	lr	3,%4		\n"
	 "	lr	6,%3		\n"
	 "	lr	7,%5		\n"
	 "	lhi	1,128		\n"
	 "	sll	1,24		\n"
	 "	or	0,1		\n"
	 "	slr	1,1		\n"
	 "	lr	2,1		\n"
	 "	lr	4,1		\n"
	 "	lr	5,1		\n"
	 "0:	.long	0xb2ae0046	\n"
	 "1:	brc	2,0b		\n"
	 "	brc	4,0b		\n"
	 "	ipm	%0		\n"
	 "	srl	%0,28		\n"
	 "	lr	%1,1		\n"
	 "	st	4,0(3)		\n"
	 "	st	5,4(3)		\n"
	 "2:				\n"
	 ".section .fixup,\"ax\"	\n"
	 "3:				\n"
	 "	lhi   %0,%h6		\n"
	 "	bras  1,4f		\n"
	 "	.long 2b		\n"
	 "4:				\n"
	 "	l     1,0(1)		\n"
	 "	br    1			\n"
	 ".previous			\n"
	 ".section __ex_table,\"a\"	\n"
	 "   .align	4		\n"
	 "   .long	0b,3b		\n"
	 "   .long	1b,3b		\n"
	 ".previous"
	 :"=d"(ccode),"=d"(*st)
	 :"d" (q_nr), "d" (rsp), "d" (id), "d" (buff_l), "K" (DEV_REC_EXCEPTION)
	 :"cc","0","1","2","3","4","5","6","7","memory");
#endif
	return ccode;
}

static inline void
itoLe2(int *i_p, unsigned char *lechars)
{
	*lechars       = *((unsigned char *) i_p + sizeof(int) - 1);
	*(lechars + 1) = *((unsigned char *) i_p + sizeof(int) - 2);
}

static inline void
le2toI(unsigned char *lechars, int *i_p)
{
	unsigned char *ic_p;
	*i_p = 0;
	ic_p = (unsigned char *) i_p;
	*(ic_p + 2) = *(lechars + 1);
	*(ic_p + 3) = *(lechars);
}

static inline int
is_empty(unsigned char *ptr, int len)
{
	return !memcmp(ptr, (unsigned char *) &static_pvt_me_key+60, len);
}

enum hdstat
query_online(int deviceNr, int cdx, int resetNr, int *q_depth, int *dev_type)
{
	int q_nr, i, t_depth, t_dev_type;
	enum devstat ccode;
	struct ap_status_word stat_word;
	enum hdstat stat;
	int break_out;

	q_nr = (deviceNr << SKIP_BITL) + cdx;
	stat = HD_BUSY;
	ccode = testq(q_nr, &t_depth, &t_dev_type, &stat_word);
	PDEBUG("ccode %d response_code %02X\n", ccode, stat_word.response_code);
	break_out = 0;
	for (i = 0; i < resetNr; i++) {
		if (ccode > 3) {
			PRINTKC("Exception testing device %d\n", i);
			return HD_TSQ_EXCEPTION;
		}
		switch (ccode) {
		case 0:
			PDEBUG("t_dev_type %d\n", t_dev_type);
			break_out = 1;
			stat = HD_ONLINE;
			*q_depth = t_depth + 1;
			switch (t_dev_type) {
			case OTHER_HW:
				stat = HD_NOT_THERE;
				*dev_type = NILDEV;
				break;
			case PCICA_HW:
				*dev_type = PCICA;
				break;
			case PCICC_HW:
				*dev_type = PCICC;
				break;
			case PCIXCC_HW:
				*dev_type = PCIXCC_UNK;
				break;
			case CEX2C_HW:
				*dev_type = CEX2C;
				break;
			default:
				*dev_type = NILDEV;
				break;
			}
			PDEBUG("available device %d: Q depth = %d, dev "
			       "type = %d, stat = %02X%02X%02X%02X\n",
			       deviceNr, *q_depth, *dev_type,
			       stat_word.q_stat_flags,
			       stat_word.response_code,
			       stat_word.reserved[0],
			       stat_word.reserved[1]);
			break;
		case 3:
			switch (stat_word.response_code) {
			case AP_RESPONSE_NORMAL:
				stat = HD_ONLINE;
				break_out = 1;
				*q_depth = t_depth + 1;
				*dev_type = t_dev_type;
				PDEBUG("cc3, available device "
				       "%d: Q depth = %d, dev "
				       "type = %d, stat = "
				       "%02X%02X%02X%02X\n",
				       deviceNr, *q_depth,
				       *dev_type,
				       stat_word.q_stat_flags,
				       stat_word.response_code,
				       stat_word.reserved[0],
				       stat_word.reserved[1]);
				break;
			case AP_RESPONSE_Q_NOT_AVAIL:
				stat = HD_NOT_THERE;
				break_out = 1;
				break;
			case AP_RESPONSE_RESET_IN_PROGRESS:
				PDEBUG("device %d in reset\n",
				       deviceNr);
				break;
			case AP_RESPONSE_DECONFIGURED:
				stat = HD_DECONFIGURED;
				break_out = 1;
				break;
			case AP_RESPONSE_CHECKSTOPPED:
				stat = HD_CHECKSTOPPED;
				break_out = 1;
				break;
			case AP_RESPONSE_BUSY:
				PDEBUG("device %d busy\n",
				       deviceNr);
				break;
			default:
				break;
			}
			break;
		default:
			stat = HD_NOT_THERE;
			break_out = 1;
			break;
		}
		if (break_out)
			break;

		udelay(5);

		ccode = testq(q_nr, &t_depth, &t_dev_type, &stat_word);
	}
	return stat;
}

enum devstat
reset_device(int deviceNr, int cdx, int resetNr)
{
	int q_nr, ccode = 0, dummy_qdepth, dummy_devType, i;
	struct ap_status_word stat_word;
	enum devstat stat;
	int break_out;

	q_nr = (deviceNr << SKIP_BITL) + cdx;
	stat = DEV_GONE;
	ccode = resetq(q_nr, &stat_word);
	if (ccode > 3)
		return DEV_RSQ_EXCEPTION;

	break_out = 0;
	for (i = 0; i < resetNr; i++) {
		switch (ccode) {
		case 0:
			stat = DEV_ONLINE;
			if (stat_word.q_stat_flags & AP_Q_STATUS_EMPTY)
				break_out = 1;
			break;
		case 3:
			switch (stat_word.response_code) {
			case AP_RESPONSE_NORMAL:
				stat = DEV_ONLINE;
				if (stat_word.q_stat_flags & AP_Q_STATUS_EMPTY)
					break_out = 1;
				break;
			case AP_RESPONSE_Q_NOT_AVAIL:
			case AP_RESPONSE_DECONFIGURED:
			case AP_RESPONSE_CHECKSTOPPED:
				stat = DEV_GONE;
				break_out = 1;
				break;
			case AP_RESPONSE_RESET_IN_PROGRESS:
			case AP_RESPONSE_BUSY:
			default:
				break;
			}
			break;
		default:
			stat = DEV_GONE;
			break_out = 1;
			break;
		}
		if (break_out == 1)
			break;
		udelay(5);

		ccode = testq(q_nr, &dummy_qdepth, &dummy_devType, &stat_word);
		if (ccode > 3) {
			stat = DEV_TSQ_EXCEPTION;
			break;
		}
	}
	PDEBUG("Number of testq's needed for reset: %d\n", i);

	if (i >= resetNr) {
	  stat = DEV_GONE;
	}

	return stat;
}

#ifdef DEBUG_HYDRA_MSGS
static inline void
print_buffer(unsigned char *buffer, int bufflen)
{
	int i;
	for (i = 0; i < bufflen; i += 16) {
		PRINTK("%04X: %02X%02X%02X%02X %02X%02X%02X%02X "
		       "%02X%02X%02X%02X %02X%02X%02X%02X\n", i,
		       buffer[i+0], buffer[i+1], buffer[i+2], buffer[i+3],
		       buffer[i+4], buffer[i+5], buffer[i+6], buffer[i+7],
		       buffer[i+8], buffer[i+9], buffer[i+10], buffer[i+11],
		       buffer[i+12], buffer[i+13], buffer[i+14], buffer[i+15]);
	}
}
#endif

enum devstat
send_to_AP(int dev_nr, int cdx, int msg_len, unsigned char *msg_ext)
{
	struct ap_status_word stat_word;
	enum devstat stat;
	int ccode;

	((struct request_msg_ext *) msg_ext)->q_nr =
		(dev_nr << SKIP_BITL) + cdx;
	PDEBUG("msg_len passed to sen: %d\n", msg_len);
	PDEBUG("q number passed to sen: %02x%02x%02x%02x\n",
	       msg_ext[0], msg_ext[1], msg_ext[2], msg_ext[3]);
	stat = DEV_GONE;

#ifdef DEBUG_HYDRA_MSGS
	PRINTK("Request header: %02X%02X%02X%02X %02X%02X%02X%02X "
	       "%02X%02X%02X%02X\n",
	       msg_ext[0], msg_ext[1], msg_ext[2], msg_ext[3],
	       msg_ext[4], msg_ext[5], msg_ext[6], msg_ext[7],
	       msg_ext[8], msg_ext[9], msg_ext[10], msg_ext[11]);
	print_buffer(msg_ext+CALLER_HEADER, msg_len);
#endif

	ccode = sen(msg_len, msg_ext, &stat_word);
	if (ccode > 3)
		return DEV_SEN_EXCEPTION;

	PDEBUG("nq cc: %u, st: %02x%02x%02x%02x\n",
	       ccode, stat_word.q_stat_flags, stat_word.response_code,
	       stat_word.reserved[0], stat_word.reserved[1]);
	switch (ccode) {
	case 0:
		stat = DEV_ONLINE;
		break;
	case 1:
		stat = DEV_GONE;
		break;
	case 3:
		switch (stat_word.response_code) {
		case AP_RESPONSE_NORMAL:
			stat = DEV_ONLINE;
			break;
		case AP_RESPONSE_Q_FULL:
			stat = DEV_QUEUE_FULL;
			break;
		default:
			stat = DEV_GONE;
			break;
		}
		break;
	default:
		stat = DEV_GONE;
		break;
	}

	return stat;
}

enum devstat
receive_from_AP(int dev_nr, int cdx, int resplen, unsigned char *resp,
		unsigned char *psmid)
{
	int ccode;
	struct ap_status_word stat_word;
	enum devstat stat;

	memset(resp, 0x00, 8);

	ccode = rec((dev_nr << SKIP_BITL) + cdx, resplen, resp, psmid,
		    &stat_word);
	if (ccode > 3)
		return DEV_REC_EXCEPTION;

	PDEBUG("dq cc: %u, st: %02x%02x%02x%02x\n",
	       ccode, stat_word.q_stat_flags, stat_word.response_code,
	       stat_word.reserved[0], stat_word.reserved[1]);

	stat = DEV_GONE;
	switch (ccode) {
	case 0:
		stat = DEV_ONLINE;
#ifdef DEBUG_HYDRA_MSGS
		print_buffer(resp, resplen);
#endif
		break;
	case 3:
		switch (stat_word.response_code) {
		case AP_RESPONSE_NORMAL:
			stat = DEV_ONLINE;
			break;
		case AP_RESPONSE_NO_PENDING_REPLY:
			if (stat_word.q_stat_flags & AP_Q_STATUS_EMPTY)
				stat = DEV_EMPTY;
			else
				stat = DEV_NO_WORK;
			break;
		case AP_RESPONSE_INDEX_TOO_BIG:
		case AP_RESPONSE_NO_FIRST_PART:
		case AP_RESPONSE_MESSAGE_TOO_BIG:
			stat = DEV_BAD_MESSAGE;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return stat;
}

static inline int
pad_msg(unsigned char *buffer, int  totalLength, int msgLength)
{
	int pad_len;

	for (pad_len = 0; pad_len < (totalLength - msgLength); pad_len++)
		if (buffer[pad_len] != 0x00)
			break;
	pad_len -= 3;
	if (pad_len < 8)
		return SEN_PAD_ERROR;

	buffer[0] = 0x00;
	buffer[1] = 0x02;

	memcpy(buffer+2, static_pad, pad_len);

	buffer[pad_len + 2] = 0x00;

	return 0;
}

static inline int
is_common_public_key(unsigned char *key, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (key[i])
			break;
	key += i;
	len -= i;
	if (((len == 1) && (key[0] == 3)) ||
	    ((len == 3) && (key[0] == 1) && (key[1] == 0) && (key[2] == 1)))
		return 1;

	return 0;
}

static int
ICAMEX_msg_to_type4MEX_msg(struct ica_rsa_modexpo *icaMex_p, int *z90cMsg_l_p,
			   union type4_msg *z90cMsg_p)
{
	int mod_len, msg_size, mod_tgt_len, exp_tgt_len, inp_tgt_len;
	unsigned char *mod_tgt, *exp_tgt, *inp_tgt;
	union type4_msg *tmp_type4_msg;

	mod_len = icaMex_p->inputdatalength;

	msg_size = ((mod_len <= 128) ? TYPE4_SME_LEN : TYPE4_LME_LEN) +
		    CALLER_HEADER;

	memset(z90cMsg_p, 0, msg_size);

	tmp_type4_msg = (union type4_msg *)
		((unsigned char *) z90cMsg_p + CALLER_HEADER);

	tmp_type4_msg->sme.header.msg_type_code = TYPE4_TYPE_CODE;
	tmp_type4_msg->sme.header.request_code = TYPE4_REQU_CODE;

	if (mod_len <= 128) {
		tmp_type4_msg->sme.header.msg_fmt = TYPE4_SME_FMT;
		tmp_type4_msg->sme.header.msg_len = TYPE4_SME_LEN;
		mod_tgt = tmp_type4_msg->sme.modulus;
		mod_tgt_len = sizeof(tmp_type4_msg->sme.modulus);
		exp_tgt = tmp_type4_msg->sme.exponent;
		exp_tgt_len = sizeof(tmp_type4_msg->sme.exponent);
		inp_tgt = tmp_type4_msg->sme.message;
		inp_tgt_len = sizeof(tmp_type4_msg->sme.message);
	} else {
		tmp_type4_msg->lme.header.msg_fmt = TYPE4_LME_FMT;
		tmp_type4_msg->lme.header.msg_len = TYPE4_LME_LEN;
		mod_tgt = tmp_type4_msg->lme.modulus;
		mod_tgt_len = sizeof(tmp_type4_msg->lme.modulus);
		exp_tgt = tmp_type4_msg->lme.exponent;
		exp_tgt_len = sizeof(tmp_type4_msg->lme.exponent);
		inp_tgt = tmp_type4_msg->lme.message;
		inp_tgt_len = sizeof(tmp_type4_msg->lme.message);
	}

	mod_tgt += (mod_tgt_len - mod_len);
	if (copy_from_user(mod_tgt, icaMex_p->n_modulus, mod_len))
		return SEN_RELEASED;
	if (is_empty(mod_tgt, mod_len))
		return SEN_USER_ERROR;
	exp_tgt += (exp_tgt_len - mod_len);
	if (copy_from_user(exp_tgt, icaMex_p->b_key, mod_len))
		return SEN_RELEASED;
	if (is_empty(exp_tgt, mod_len))
		return SEN_USER_ERROR;
	inp_tgt += (inp_tgt_len - mod_len);
	if (copy_from_user(inp_tgt, icaMex_p->inputdata, mod_len))
		return SEN_RELEASED;
	if (is_empty(inp_tgt, mod_len))
		return SEN_USER_ERROR;

	*z90cMsg_l_p = msg_size - CALLER_HEADER;

	return 0;
}

static int
ICACRT_msg_to_type4CRT_msg(struct ica_rsa_modexpo_crt *icaMsg_p,
			   int *z90cMsg_l_p, union type4_msg *z90cMsg_p)
{
	int mod_len, short_len, long_len, tmp_size, p_tgt_len, q_tgt_len,
	    dp_tgt_len, dq_tgt_len, u_tgt_len, inp_tgt_len;
	unsigned char *p_tgt, *q_tgt, *dp_tgt, *dq_tgt, *u_tgt, *inp_tgt;
	union type4_msg *tmp_type4_msg;

	mod_len = icaMsg_p->inputdatalength;
	short_len = mod_len / 2;
	long_len = mod_len / 2 + 8;

	tmp_size = ((mod_len <= 128) ? TYPE4_SCR_LEN : TYPE4_LCR_LEN) +
		    CALLER_HEADER;

	memset(z90cMsg_p, 0, tmp_size);

	tmp_type4_msg = (union type4_msg *)
		((unsigned char *) z90cMsg_p + CALLER_HEADER);

	tmp_type4_msg->scr.header.msg_type_code = TYPE4_TYPE_CODE;
	tmp_type4_msg->scr.header.request_code = TYPE4_REQU_CODE;
	if (mod_len <= 128) {
		tmp_type4_msg->scr.header.msg_fmt = TYPE4_SCR_FMT;
		tmp_type4_msg->scr.header.msg_len = TYPE4_SCR_LEN;
		p_tgt = tmp_type4_msg->scr.p;
		p_tgt_len = sizeof(tmp_type4_msg->scr.p);
		q_tgt = tmp_type4_msg->scr.q;
		q_tgt_len = sizeof(tmp_type4_msg->scr.q);
		dp_tgt = tmp_type4_msg->scr.dp;
		dp_tgt_len = sizeof(tmp_type4_msg->scr.dp);
		dq_tgt = tmp_type4_msg->scr.dq;
		dq_tgt_len = sizeof(tmp_type4_msg->scr.dq);
		u_tgt = tmp_type4_msg->scr.u;
		u_tgt_len = sizeof(tmp_type4_msg->scr.u);
		inp_tgt = tmp_type4_msg->scr.message;
		inp_tgt_len = sizeof(tmp_type4_msg->scr.message);
	} else {
		tmp_type4_msg->lcr.header.msg_fmt = TYPE4_LCR_FMT;
		tmp_type4_msg->lcr.header.msg_len = TYPE4_LCR_LEN;
		p_tgt = tmp_type4_msg->lcr.p;
		p_tgt_len = sizeof(tmp_type4_msg->lcr.p);
		q_tgt = tmp_type4_msg->lcr.q;
		q_tgt_len = sizeof(tmp_type4_msg->lcr.q);
		dp_tgt = tmp_type4_msg->lcr.dp;
		dp_tgt_len = sizeof(tmp_type4_msg->lcr.dp);
		dq_tgt = tmp_type4_msg->lcr.dq;
		dq_tgt_len = sizeof(tmp_type4_msg->lcr.dq);
		u_tgt = tmp_type4_msg->lcr.u;
		u_tgt_len = sizeof(tmp_type4_msg->lcr.u);
		inp_tgt = tmp_type4_msg->lcr.message;
		inp_tgt_len = sizeof(tmp_type4_msg->lcr.message);
	}

	p_tgt += (p_tgt_len - long_len);
	if (copy_from_user(p_tgt, icaMsg_p->np_prime, long_len))
		return SEN_RELEASED;
	if (is_empty(p_tgt, long_len))
		return SEN_USER_ERROR;
	q_tgt += (q_tgt_len - short_len);
	if (copy_from_user(q_tgt, icaMsg_p->nq_prime, short_len))
		return SEN_RELEASED;
	if (is_empty(q_tgt, short_len))
		return SEN_USER_ERROR;
	dp_tgt += (dp_tgt_len - long_len);
	if (copy_from_user(dp_tgt, icaMsg_p->bp_key, long_len))
		return SEN_RELEASED;
	if (is_empty(dp_tgt, long_len))
		return SEN_USER_ERROR;
	dq_tgt += (dq_tgt_len - short_len);
	if (copy_from_user(dq_tgt, icaMsg_p->bq_key, short_len))
		return SEN_RELEASED;
	if (is_empty(dq_tgt, short_len))
		return SEN_USER_ERROR;
	u_tgt += (u_tgt_len - long_len);
	if (copy_from_user(u_tgt, icaMsg_p->u_mult_inv, long_len))
		return SEN_RELEASED;
	if (is_empty(u_tgt, long_len))
		return SEN_USER_ERROR;
	inp_tgt += (inp_tgt_len - mod_len);
	if (copy_from_user(inp_tgt, icaMsg_p->inputdata, mod_len))
		return SEN_RELEASED;
	if (is_empty(inp_tgt, mod_len))
		return SEN_USER_ERROR;

	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

static int
ICAMEX_msg_to_type6MEX_de_msg(struct ica_rsa_modexpo *icaMsg_p, int cdx,
			      int *z90cMsg_l_p, struct type6_msg *z90cMsg_p)
{
	int mod_len, vud_len, tmp_size, total_CPRB_len, parmBlock_l;
	unsigned char *temp;
	struct type6_hdr *tp6Hdr_p;
	struct CPRB *cprb_p;
	struct cca_private_ext_ME *key_p;
	static int deprecated_msg_count = 0;

	mod_len = icaMsg_p->inputdatalength;
	tmp_size = FIXED_TYPE6_ME_LEN + mod_len;
	total_CPRB_len = tmp_size - sizeof(struct type6_hdr);
	parmBlock_l = total_CPRB_len - sizeof(struct CPRB);
	tmp_size = 4*((tmp_size + 3)/4) + CALLER_HEADER;

	memset(z90cMsg_p, 0, tmp_size);

	temp = (unsigned char *)z90cMsg_p + CALLER_HEADER;
	memcpy(temp, &static_type6_hdr, sizeof(struct type6_hdr));
	tp6Hdr_p = (struct type6_hdr *)temp;
	tp6Hdr_p->ToCardLen1 = 4*((total_CPRB_len+3)/4);
	tp6Hdr_p->FromCardLen1 = RESPONSE_CPRB_SIZE;

	temp += sizeof(struct type6_hdr);
	memcpy(temp, &static_cprb, sizeof(struct CPRB));
	cprb_p = (struct CPRB *) temp;
	cprb_p->usage_domain[0]= (unsigned char)cdx;
	itoLe2(&parmBlock_l, cprb_p->req_parml);
	itoLe2((int *)&(tp6Hdr_p->FromCardLen1), cprb_p->rpl_parml);

	temp += sizeof(struct CPRB);
	memcpy(temp, &static_pkd_function_and_rules,
	       sizeof(struct function_and_rules_block));

	temp += sizeof(struct function_and_rules_block);
	vud_len = 2 + icaMsg_p->inputdatalength;
	itoLe2(&vud_len, temp);

	temp += 2;
	if (copy_from_user(temp, icaMsg_p->inputdata, mod_len))
		return SEN_RELEASED;
	if (is_empty(temp, mod_len))
		return SEN_USER_ERROR;

	temp += mod_len;
	memcpy(temp, &static_T6_keyBlock_hdr, sizeof(struct T6_keyBlock_hdr));

	temp += sizeof(struct T6_keyBlock_hdr);
	memcpy(temp, &static_pvt_me_key, sizeof(struct cca_private_ext_ME));
	key_p = (struct cca_private_ext_ME *)temp;
	temp = key_p->pvtMESec.exponent + sizeof(key_p->pvtMESec.exponent)
	       - mod_len;
	if (copy_from_user(temp, icaMsg_p->b_key, mod_len))
		return SEN_RELEASED;
	if (is_empty(temp, mod_len))
		return SEN_USER_ERROR;

	if (is_common_public_key(temp, mod_len)) {
		if (deprecated_msg_count < 20) {
			PRINTK("Common public key used for modex decrypt\n");
			deprecated_msg_count++;
			if (deprecated_msg_count == 20)
				PRINTK("No longer issuing messages about common"
				       " public key for modex decrypt.\n");
		}
		return SEN_NOT_AVAIL;
	}

	temp = key_p->pvtMESec.modulus + sizeof(key_p->pvtMESec.modulus)
	       - mod_len;
	if (copy_from_user(temp, icaMsg_p->n_modulus, mod_len))
		return SEN_RELEASED;
	if (is_empty(temp, mod_len))
		return SEN_USER_ERROR;

	key_p->pubMESec.modulus_bit_len = 8 * mod_len;

	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

static int
ICAMEX_msg_to_type6MEX_en_msg(struct ica_rsa_modexpo *icaMsg_p, int cdx,
			      int *z90cMsg_l_p, struct type6_msg *z90cMsg_p)
{
	int mod_len, vud_len, exp_len, key_len;
	int pad_len, tmp_size, total_CPRB_len, parmBlock_l, i;
	unsigned char *temp_exp, *exp_p, *temp;
	struct type6_hdr *tp6Hdr_p;
	struct CPRB *cprb_p;
	struct cca_public_key *key_p;
	struct T6_keyBlock_hdr *keyb_p;

	temp_exp = kmalloc(256, GFP_KERNEL);
	if (!temp_exp)
		return EGETBUFF;
	mod_len = icaMsg_p->inputdatalength;
	if (copy_from_user(temp_exp, icaMsg_p->b_key, mod_len)) {
		kfree(temp_exp);
		return SEN_RELEASED;
	}
	if (is_empty(temp_exp, mod_len)) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}

	exp_p = temp_exp;
	for (i = 0; i < mod_len; i++)
		if (exp_p[i])
			break;
	if (i >= mod_len) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}

	exp_len = mod_len - i;
	exp_p += i;

	PDEBUG("exp_len after computation: %08x\n", exp_len);
	tmp_size = FIXED_TYPE6_ME_EN_LEN + 2 * mod_len + exp_len;
	total_CPRB_len = tmp_size - sizeof(struct type6_hdr);
	parmBlock_l = total_CPRB_len - sizeof(struct CPRB);
	tmp_size = 4*((tmp_size + 3)/4) + CALLER_HEADER;

	vud_len = 2 + mod_len;
	memset(z90cMsg_p, 0, tmp_size);

	temp = (unsigned char *)z90cMsg_p + CALLER_HEADER;
	memcpy(temp, &static_type6_hdr, sizeof(struct type6_hdr));
	tp6Hdr_p = (struct type6_hdr *)temp;
	tp6Hdr_p->ToCardLen1 = 4*((total_CPRB_len+3)/4);
	tp6Hdr_p->FromCardLen1 = RESPONSE_CPRB_SIZE;
	memcpy(tp6Hdr_p->function_code, static_PKE_function_code,
	       sizeof(static_PKE_function_code));
	temp += sizeof(struct type6_hdr);
	memcpy(temp, &static_cprb, sizeof(struct CPRB));
	cprb_p = (struct CPRB *) temp;
	cprb_p->usage_domain[0]= (unsigned char)cdx;
	itoLe2((int *)&(tp6Hdr_p->FromCardLen1), cprb_p->rpl_parml);
	temp += sizeof(struct CPRB);
	memcpy(temp, &static_pke_function_and_rules,
		 sizeof(struct function_and_rules_block));
	temp += sizeof(struct function_and_rules_block);
	temp += 2;
	if (copy_from_user(temp, icaMsg_p->inputdata, mod_len)) {
		kfree(temp_exp);
		return SEN_RELEASED;
	}
	if (is_empty(temp, mod_len)) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}
	if ((temp[0] != 0x00) || (temp[1] != 0x02)) {
		kfree(temp_exp);
		return SEN_NOT_AVAIL;
	}
	for (i = 2; i < mod_len; i++)
		if (temp[i] == 0x00)
			break;
	if ((i < 9) || (i > (mod_len - 2))) {
		kfree(temp_exp);
		return SEN_NOT_AVAIL;
	}
	pad_len = i + 1;
	vud_len = mod_len - pad_len;
	memmove(temp, temp+pad_len, vud_len);
	temp -= 2;
	vud_len += 2;
	itoLe2(&vud_len, temp);
	temp += (vud_len);
	keyb_p = (struct T6_keyBlock_hdr *)temp;
	temp += sizeof(struct T6_keyBlock_hdr);
	memcpy(temp, &static_public_key, sizeof(static_public_key));
	key_p = (struct cca_public_key *)temp;
	temp = key_p->pubSec.exponent;
	memcpy(temp, exp_p, exp_len);
	kfree(temp_exp);
	temp += exp_len;
	if (copy_from_user(temp, icaMsg_p->n_modulus, mod_len))
		return SEN_RELEASED;
	if (is_empty(temp, mod_len))
		return SEN_USER_ERROR;
	key_p->pubSec.modulus_bit_len = 8 * mod_len;
	key_p->pubSec.modulus_byte_len = mod_len;
	key_p->pubSec.exponent_len = exp_len;
	key_p->pubSec.section_length = CALLER_HEADER + mod_len + exp_len;
	key_len = key_p->pubSec.section_length + sizeof(struct cca_token_hdr);
	key_p->pubHdr.token_length = key_len;
	key_len += 4;
	itoLe2(&key_len, keyb_p->ulen);
	key_len += 2;
	itoLe2(&key_len, keyb_p->blen);
	parmBlock_l -= pad_len;
	itoLe2(&parmBlock_l, cprb_p->req_parml);
	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

static int
ICACRT_msg_to_type6CRT_msg(struct ica_rsa_modexpo_crt *icaMsg_p, int cdx,
			   int *z90cMsg_l_p, struct type6_msg *z90cMsg_p)
{
	int mod_len, vud_len, tmp_size, total_CPRB_len, parmBlock_l, short_len;
	int long_len, pad_len, keyPartsLen, tmp_l;
	unsigned char *tgt_p, *temp;
	struct type6_hdr *tp6Hdr_p;
	struct CPRB *cprb_p;
	struct cca_token_hdr *keyHdr_p;
	struct cca_pvt_ext_CRT_sec *pvtSec_p;
	struct cca_public_sec *pubSec_p;

	mod_len = icaMsg_p->inputdatalength;
	short_len = mod_len / 2;
	long_len = 8 + short_len;
	keyPartsLen = 3 * long_len + 2 * short_len;
	pad_len = (8 - (keyPartsLen % 8)) % 8;
	keyPartsLen += pad_len + mod_len;
	tmp_size = FIXED_TYPE6_CR_LEN + keyPartsLen + mod_len;
	total_CPRB_len = tmp_size -  sizeof(struct type6_hdr);
	parmBlock_l = total_CPRB_len - sizeof(struct CPRB);
	vud_len = 2 + mod_len;
	tmp_size = 4*((tmp_size + 3)/4) + CALLER_HEADER;

	memset(z90cMsg_p, 0, tmp_size);
	tgt_p = (unsigned char *)z90cMsg_p + CALLER_HEADER;
	memcpy(tgt_p, &static_type6_hdr, sizeof(struct type6_hdr));
	tp6Hdr_p = (struct type6_hdr *)tgt_p;
	tp6Hdr_p->ToCardLen1 = 4*((total_CPRB_len+3)/4);
	tp6Hdr_p->FromCardLen1 = RESPONSE_CPRB_SIZE;
	tgt_p += sizeof(struct type6_hdr);
	cprb_p = (struct CPRB *) tgt_p;
	memcpy(tgt_p, &static_cprb, sizeof(struct CPRB));
	cprb_p->usage_domain[0]= *((unsigned char *)(&(cdx))+3);
	itoLe2(&parmBlock_l, cprb_p->req_parml);
	memcpy(cprb_p->rpl_parml, cprb_p->req_parml,
	       sizeof(cprb_p->req_parml));
	tgt_p += sizeof(struct CPRB);
	memcpy(tgt_p, &static_pkd_function_and_rules,
	       sizeof(struct function_and_rules_block));
	tgt_p += sizeof(struct function_and_rules_block);
	itoLe2(&vud_len, tgt_p);
	tgt_p += 2;
	if (copy_from_user(tgt_p, icaMsg_p->inputdata, mod_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, mod_len))
		return SEN_USER_ERROR;
	tgt_p += mod_len;
	tmp_l = sizeof(struct T6_keyBlock_hdr) + sizeof(struct cca_token_hdr) +
		sizeof(struct cca_pvt_ext_CRT_sec) + 0x0F + keyPartsLen;
	itoLe2(&tmp_l, tgt_p);
	temp = tgt_p + 2;
	tmp_l -= 2;
	itoLe2(&tmp_l, temp);
	tgt_p += sizeof(struct T6_keyBlock_hdr);
	keyHdr_p = (struct cca_token_hdr *)tgt_p;
	keyHdr_p->token_identifier = CCA_TKN_HDR_ID_EXT;
	tmp_l -= 4;
	keyHdr_p->token_length = tmp_l;
	tgt_p += sizeof(struct cca_token_hdr);
	pvtSec_p = (struct cca_pvt_ext_CRT_sec *)tgt_p;
	pvtSec_p->section_identifier = CCA_PVT_EXT_CRT_SEC_ID_PVT;
	pvtSec_p->section_length =
		sizeof(struct cca_pvt_ext_CRT_sec) + keyPartsLen;
	pvtSec_p->key_format = CCA_PVT_EXT_CRT_SEC_FMT_CL;
	pvtSec_p->key_use_flags[0] = CCA_PVT_USAGE_ALL;
	pvtSec_p->p_len = long_len;
	pvtSec_p->q_len = short_len;
	pvtSec_p->dp_len = long_len;
	pvtSec_p->dq_len = short_len;
	pvtSec_p->u_len = long_len;
	pvtSec_p->mod_len = mod_len;
	pvtSec_p->pad_len = pad_len;
	tgt_p += sizeof(struct cca_pvt_ext_CRT_sec);
	if (copy_from_user(tgt_p, icaMsg_p->np_prime, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	if (copy_from_user(tgt_p, icaMsg_p->nq_prime, short_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, short_len))
		return SEN_USER_ERROR;
	tgt_p += short_len;
	if (copy_from_user(tgt_p, icaMsg_p->bp_key, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	if (copy_from_user(tgt_p, icaMsg_p->bq_key, short_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, short_len))
		return SEN_USER_ERROR;
	tgt_p += short_len;
	if (copy_from_user(tgt_p, icaMsg_p->u_mult_inv, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	tgt_p += pad_len;
	memset(tgt_p, 0xFF, mod_len);
	tgt_p += mod_len;
	memcpy(tgt_p, &static_cca_pub_sec, sizeof(struct cca_public_sec));
	pubSec_p = (struct cca_public_sec *) tgt_p;
	pubSec_p->modulus_bit_len = 8 * mod_len;
	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

static int
ICAMEX_msg_to_type6MEX_msgX(struct ica_rsa_modexpo *icaMsg_p, int cdx,
			    int *z90cMsg_l_p, struct type6_msg *z90cMsg_p,
			    int dev_type)
{
	int mod_len, exp_len, vud_len, tmp_size, total_CPRB_len, parmBlock_l;
	int key_len, i;
	unsigned char *temp_exp, *tgt_p, *temp, *exp_p;
	struct type6_hdr *tp6Hdr_p;
	struct CPRBX *cprbx_p;
	struct cca_public_key *key_p;
	struct T6_keyBlock_hdrX *keyb_p;

	temp_exp = kmalloc(256, GFP_KERNEL);
	if (!temp_exp)
		return EGETBUFF;
	mod_len = icaMsg_p->inputdatalength;
	if (copy_from_user(temp_exp, icaMsg_p->b_key, mod_len)) {
		kfree(temp_exp);
		return SEN_RELEASED;
	}
	if (is_empty(temp_exp, mod_len)) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}
	exp_p = temp_exp;
	for (i = 0; i < mod_len; i++)
		if (exp_p[i])
			break;
	if (i >= mod_len) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}
	exp_len = mod_len - i;
	exp_p += i;
	PDEBUG("exp_len after computation: %08x\n", exp_len);
	tmp_size = FIXED_TYPE6_ME_EN_LENX + 2 * mod_len + exp_len;
	total_CPRB_len = tmp_size - sizeof(struct type6_hdr);
	parmBlock_l = total_CPRB_len - sizeof(struct CPRBX);
	tmp_size = tmp_size + CALLER_HEADER;
	vud_len = 2 + mod_len;
	memset(z90cMsg_p, 0, tmp_size);
	tgt_p = (unsigned char *)z90cMsg_p + CALLER_HEADER;
	memcpy(tgt_p, &static_type6_hdrX, sizeof(struct type6_hdr));
	tp6Hdr_p = (struct type6_hdr *)tgt_p;
	tp6Hdr_p->ToCardLen1 = total_CPRB_len;
	tp6Hdr_p->FromCardLen1 = RESPONSE_CPRBX_SIZE;
	memcpy(tp6Hdr_p->function_code, static_PKE_function_code,
	       sizeof(static_PKE_function_code));
	tgt_p += sizeof(struct type6_hdr);
	memcpy(tgt_p, &static_cprbx, sizeof(struct CPRBX));
	cprbx_p = (struct CPRBX *) tgt_p;
	cprbx_p->domain = (unsigned short)cdx;
	cprbx_p->rpl_msgbl = RESPONSE_CPRBX_SIZE;
	tgt_p += sizeof(struct CPRBX);
	if (dev_type == PCIXCC_MCL2)
		memcpy(tgt_p, &static_pke_function_and_rulesX_MCL2,
		       sizeof(struct function_and_rules_block));
	else
		memcpy(tgt_p, &static_pke_function_and_rulesX,
		       sizeof(struct function_and_rules_block));
	tgt_p += sizeof(struct function_and_rules_block);

	tgt_p += 2;
	if (copy_from_user(tgt_p, icaMsg_p->inputdata, mod_len)) {
		kfree(temp_exp);
		return SEN_RELEASED;
	}
	if (is_empty(tgt_p, mod_len)) {
		kfree(temp_exp);
		return SEN_USER_ERROR;
	}
	tgt_p -= 2;
	*((short *)tgt_p) = (short) vud_len;
	tgt_p += vud_len;
	keyb_p = (struct T6_keyBlock_hdrX *)tgt_p;
	tgt_p += sizeof(struct T6_keyBlock_hdrX);
	memcpy(tgt_p, &static_public_key, sizeof(static_public_key));
	key_p = (struct cca_public_key *)tgt_p;
	temp = key_p->pubSec.exponent;
	memcpy(temp, exp_p, exp_len);
	kfree(temp_exp);
	temp += exp_len;
	if (copy_from_user(temp, icaMsg_p->n_modulus, mod_len))
		return SEN_RELEASED;
	if (is_empty(temp, mod_len))
		return SEN_USER_ERROR;
	key_p->pubSec.modulus_bit_len = 8 * mod_len;
	key_p->pubSec.modulus_byte_len = mod_len;
	key_p->pubSec.exponent_len = exp_len;
	key_p->pubSec.section_length = CALLER_HEADER + mod_len + exp_len;
	key_len = key_p->pubSec.section_length + sizeof(struct cca_token_hdr);
	key_p->pubHdr.token_length = key_len;
	key_len += 4;
	keyb_p->ulen = (unsigned short)key_len;
	key_len += 2;
	keyb_p->blen = (unsigned short)key_len;
	cprbx_p->req_parml = parmBlock_l;
	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

static int
ICACRT_msg_to_type6CRT_msgX(struct ica_rsa_modexpo_crt *icaMsg_p, int cdx,
			    int *z90cMsg_l_p, struct type6_msg *z90cMsg_p,
			    int dev_type)
{
	int mod_len, vud_len, tmp_size, total_CPRB_len, parmBlock_l, short_len;
	int long_len, pad_len, keyPartsLen, tmp_l;
	unsigned char *tgt_p, *temp;
	struct type6_hdr *tp6Hdr_p;
	struct CPRBX *cprbx_p;
	struct cca_token_hdr *keyHdr_p;
	struct cca_pvt_ext_CRT_sec *pvtSec_p;
	struct cca_public_sec *pubSec_p;

	mod_len = icaMsg_p->inputdatalength;
	short_len = mod_len / 2;
	long_len = 8 + short_len;
	keyPartsLen = 3 * long_len + 2 * short_len;
	pad_len = (8 - (keyPartsLen % 8)) % 8;
	keyPartsLen += pad_len + mod_len;
	tmp_size = FIXED_TYPE6_CR_LENX + keyPartsLen + mod_len;
	total_CPRB_len = tmp_size -  sizeof(struct type6_hdr);
	parmBlock_l = total_CPRB_len - sizeof(struct CPRBX);
	vud_len = 2 + mod_len;
	tmp_size = tmp_size + CALLER_HEADER;
	memset(z90cMsg_p, 0, tmp_size);
	tgt_p = (unsigned char *)z90cMsg_p + CALLER_HEADER;
	memcpy(tgt_p, &static_type6_hdrX, sizeof(struct type6_hdr));
	tp6Hdr_p = (struct type6_hdr *)tgt_p;
	tp6Hdr_p->ToCardLen1 = total_CPRB_len;
	tp6Hdr_p->FromCardLen1 = RESPONSE_CPRBX_SIZE;
	tgt_p += sizeof(struct type6_hdr);
	cprbx_p = (struct CPRBX *) tgt_p;
	memcpy(tgt_p, &static_cprbx, sizeof(struct CPRBX));
	cprbx_p->domain = (unsigned short)cdx;
	cprbx_p->req_parml = parmBlock_l;
	cprbx_p->rpl_msgbl = parmBlock_l;
	tgt_p += sizeof(struct CPRBX);
	if (dev_type == PCIXCC_MCL2)
		memcpy(tgt_p, &static_pkd_function_and_rulesX_MCL2,
		       sizeof(struct function_and_rules_block));
	else
		memcpy(tgt_p, &static_pkd_function_and_rulesX,
		       sizeof(struct function_and_rules_block));
	tgt_p += sizeof(struct function_and_rules_block);
	*((short *)tgt_p) = (short) vud_len;
	tgt_p += 2;
	if (copy_from_user(tgt_p, icaMsg_p->inputdata, mod_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, mod_len))
		return SEN_USER_ERROR;
	tgt_p += mod_len;
	tmp_l = sizeof(struct T6_keyBlock_hdr) + sizeof(struct cca_token_hdr) +
		sizeof(struct cca_pvt_ext_CRT_sec) + 0x0F + keyPartsLen;
	*((short *)tgt_p) = (short) tmp_l;
	temp = tgt_p + 2;
	tmp_l -= 2;
	*((short *)temp) = (short) tmp_l;
	tgt_p += sizeof(struct T6_keyBlock_hdr);
	keyHdr_p = (struct cca_token_hdr *)tgt_p;
	keyHdr_p->token_identifier = CCA_TKN_HDR_ID_EXT;
	tmp_l -= 4;
	keyHdr_p->token_length = tmp_l;
	tgt_p += sizeof(struct cca_token_hdr);
	pvtSec_p = (struct cca_pvt_ext_CRT_sec *)tgt_p;
	pvtSec_p->section_identifier = CCA_PVT_EXT_CRT_SEC_ID_PVT;
	pvtSec_p->section_length =
		sizeof(struct cca_pvt_ext_CRT_sec) + keyPartsLen;
	pvtSec_p->key_format = CCA_PVT_EXT_CRT_SEC_FMT_CL;
	pvtSec_p->key_use_flags[0] = CCA_PVT_USAGE_ALL;
	pvtSec_p->p_len = long_len;
	pvtSec_p->q_len = short_len;
	pvtSec_p->dp_len = long_len;
	pvtSec_p->dq_len = short_len;
	pvtSec_p->u_len = long_len;
	pvtSec_p->mod_len = mod_len;
	pvtSec_p->pad_len = pad_len;
	tgt_p += sizeof(struct cca_pvt_ext_CRT_sec);
	if (copy_from_user(tgt_p, icaMsg_p->np_prime, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	if (copy_from_user(tgt_p, icaMsg_p->nq_prime, short_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, short_len))
		return SEN_USER_ERROR;
	tgt_p += short_len;
	if (copy_from_user(tgt_p, icaMsg_p->bp_key, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	if (copy_from_user(tgt_p, icaMsg_p->bq_key, short_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, short_len))
		return SEN_USER_ERROR;
	tgt_p += short_len;
	if (copy_from_user(tgt_p, icaMsg_p->u_mult_inv, long_len))
		return SEN_RELEASED;
	if (is_empty(tgt_p, long_len))
		return SEN_USER_ERROR;
	tgt_p += long_len;
	tgt_p += pad_len;
	memset(tgt_p, 0xFF, mod_len);
	tgt_p += mod_len;
	memcpy(tgt_p, &static_cca_pub_sec, sizeof(struct cca_public_sec));
	pubSec_p = (struct cca_public_sec *) tgt_p;
	pubSec_p->modulus_bit_len = 8 * mod_len;
	*z90cMsg_l_p = tmp_size - CALLER_HEADER;

	return 0;
}

int
convert_request(unsigned char *buffer, int func, unsigned short function,
		int cdx, int dev_type, int *msg_l_p, unsigned char *msg_p)
{
	if (dev_type == PCICA) {
		if (func == ICARSACRT)
			return ICACRT_msg_to_type4CRT_msg(
				(struct ica_rsa_modexpo_crt *) buffer,
				msg_l_p, (union type4_msg *) msg_p);
		else
			return ICAMEX_msg_to_type4MEX_msg(
				(struct ica_rsa_modexpo *) buffer,
				msg_l_p, (union type4_msg *) msg_p);
	}
	if (dev_type == PCICC) {
		if (func == ICARSACRT)
			return ICACRT_msg_to_type6CRT_msg(
				(struct ica_rsa_modexpo_crt *) buffer,
				cdx, msg_l_p, (struct type6_msg *)msg_p);
		if (function == PCI_FUNC_KEY_ENCRYPT)
			return ICAMEX_msg_to_type6MEX_en_msg(
				(struct ica_rsa_modexpo *) buffer,
				cdx, msg_l_p, (struct type6_msg *) msg_p);
		else
			return ICAMEX_msg_to_type6MEX_de_msg(
				(struct ica_rsa_modexpo *) buffer,
				cdx, msg_l_p, (struct type6_msg *) msg_p);
	}
	if ((dev_type == PCIXCC_MCL2) ||
	    (dev_type == PCIXCC_MCL3) ||
	    (dev_type == CEX2C)) {
		if (func == ICARSACRT)
			return ICACRT_msg_to_type6CRT_msgX(
				(struct ica_rsa_modexpo_crt *) buffer,
				cdx, msg_l_p, (struct type6_msg *) msg_p,
				dev_type);
		else
			return ICAMEX_msg_to_type6MEX_msgX(
				(struct ica_rsa_modexpo *) buffer,
				cdx, msg_l_p, (struct type6_msg *) msg_p,
				dev_type);
	}

	return 0;
}

int ext_bitlens_msg_count = 0;
static inline void
unset_ext_bitlens(void)
{
	if (!ext_bitlens_msg_count) {
		PRINTK("Unable to use coprocessors for extended bitlengths. "
		       "Using PCICAs (if present) for extended bitlengths. "
		       "This is not an error.\n");
		ext_bitlens_msg_count++;
	}
	ext_bitlens = 0;
}

int
convert_response(unsigned char *response, unsigned char *buffer,
		 int *respbufflen_p, unsigned char *resp_buff)
{
	struct ica_rsa_modexpo *icaMsg_p = (struct ica_rsa_modexpo *) buffer;
	struct type82_hdr *t82h_p = (struct type82_hdr *) response;
	struct type84_hdr *t84h_p = (struct type84_hdr *) response;
	struct type86_fmt2_msg *t86m_p =  (struct type86_fmt2_msg *) response;
	int reply_code, service_rc, service_rs, src_l;
	unsigned char *src_p, *tgt_p;
	struct CPRB *cprb_p;
	struct CPRBX *cprbx_p;

	src_p = 0;
	reply_code = 0;
	service_rc = 0;
	service_rs = 0;
	src_l = 0;
	switch (t82h_p->type) {
	case TYPE82_RSP_CODE:
		reply_code = t82h_p->reply_code;
		src_p = (unsigned char *)t82h_p;
		PRINTK("Hardware error: Type 82 Message Header: "
		       "%02x%02x%02x%02x%02x%02x%02x%02x\n",
		       src_p[0], src_p[1], src_p[2], src_p[3],
		       src_p[4], src_p[5], src_p[6], src_p[7]);
		break;
	case TYPE84_RSP_CODE:
		src_l = icaMsg_p->outputdatalength;
		src_p = response + (int)t84h_p->len - src_l;
		break;
	case TYPE86_RSP_CODE:
		reply_code = t86m_p->hdr.reply_code;
		if (reply_code != 0)
			break;
		cprb_p = (struct CPRB *)
			(response + sizeof(struct type86_fmt2_msg));
		cprbx_p = (struct CPRBX *) cprb_p;
		if (cprb_p->cprb_ver_id != 0x02) {
			le2toI(cprb_p->ccp_rtcode, &service_rc);
			if (service_rc != 0) {
				le2toI(cprb_p->ccp_rscode, &service_rs);
				if ((service_rc == 8) && (service_rs == 66))
					PDEBUG("Bad block format on PCICC\n");
				else if ((service_rc == 8) && (service_rs == 770)) {
					PDEBUG("Invalid key length on PCICC\n");
					unset_ext_bitlens();
					return REC_USE_PCICA;
				}
				else if ((service_rc == 8) && (service_rs == 783)) {
					PDEBUG("Extended bitlengths not enabled"
					       "on PCICC\n");
					unset_ext_bitlens();
					return REC_USE_PCICA;
				}
				else
					PRINTK("service rc/rs: %d/%d\n",
					       service_rc, service_rs);
				return REC_OPERAND_INV;
			}
			src_p = (unsigned char *)cprb_p + sizeof(struct CPRB);
			src_p += 4;
			le2toI(src_p, &src_l);
			src_l -= 2;
			src_p += 2;
		} else {
			service_rc = (int)cprbx_p->ccp_rtcode;
			if (service_rc != 0) {
				service_rs = (int) cprbx_p->ccp_rscode;
				if ((service_rc == 8) && (service_rs == 66))
					PDEBUG("Bad block format on PCXICC\n");
				else if ((service_rc == 8) && (service_rs == 770)) {
					PDEBUG("Invalid key length on PCIXCC\n");
					unset_ext_bitlens();
					return REC_USE_PCICA;
				}
				else if ((service_rc == 8) && (service_rs == 783)) {
					PDEBUG("Extended bitlengths not enabled"
					       "on PCIXCC\n");
					unset_ext_bitlens();
					return REC_USE_PCICA;
				}
				else
					PRINTK("service rc/rs: %d/%d\n",
					       service_rc, service_rs);
				return REC_OPERAND_INV;
			}
			src_p = (unsigned char *)
				cprbx_p + sizeof(struct CPRBX);
			src_p += 4;
			src_l = (int)(*((short *) src_p));
			src_l -= 2;
			src_p += 2;
		}
		break;
	default:
		return REC_BAD_MESSAGE;
	}

	if (reply_code)
		switch (reply_code) {
		case REPLY_ERROR_OPERAND_INVALID:
			return REC_OPERAND_INV;
		case REPLY_ERROR_OPERAND_SIZE:
			return REC_OPERAND_SIZE;
		case REPLY_ERROR_EVEN_MOD_IN_OPND:
			return REC_EVEN_MOD;
		case REPLY_ERROR_MESSAGE_TYPE:
			return WRONG_DEVICE_TYPE;
		case REPLY_ERROR_TRANSPORT_FAIL:
			PRINTKW("Transport failed (APFS = %02X%02X%02X%02X)\n",
				t86m_p->apfs[0], t86m_p->apfs[1],
				t86m_p->apfs[2], t86m_p->apfs[3]);
			return REC_HARDWAR_ERR;
		default:
			PRINTKW("reply code = %d\n", reply_code);
			return REC_HARDWAR_ERR;
		}

	if (service_rc != 0)
		return REC_OPERAND_INV;

	if ((src_l > icaMsg_p->outputdatalength) ||
	    (src_l > RESPBUFFSIZE) ||
	    (src_l <= 0))
		return REC_OPERAND_SIZE;

	PDEBUG("Length returned = %d\n", src_l);
	tgt_p = resp_buff + icaMsg_p->outputdatalength - src_l;
	memcpy(tgt_p, src_p, src_l);
	if ((t82h_p->type == TYPE86_RSP_CODE) && (resp_buff < tgt_p)) {
		memset(resp_buff, 0, icaMsg_p->outputdatalength - src_l);
		if (pad_msg(resp_buff, icaMsg_p->outputdatalength, src_l))
			return REC_INVALID_PAD;
	}
	*respbufflen_p = icaMsg_p->outputdatalength;
	if (*respbufflen_p == 0)
		PRINTK("Zero *respbufflen_p\n");

	return 0;
}

