/*
 * Copyright (C) 2007-2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>

#define IS_CPCAP(reg) ((reg) >= CPCAP_REG_START && (reg) <= CPCAP_REG_END)

static DEFINE_MUTEX(reg_access);

/*
 * This table contains information about a single register in the power IC.
 * It is used during register access to information such as the register address
 * and the modifiability of each bit in the register.  Special notes for
 * particular elements of this structure follows:
 *
 * constant_mask: A '1' in this mask indicates that the corresponding bit has a
 * 'constant' modifiability, and therefore must never be changed by any register
 * access.
 *
 * It is important to note that any bits that are 'constant' must have
 * synchronized read/write values.  That is to say, when a 'constant' bit is
 * read the value read must be identical to the value that must be written to
 * that bit in order for that bit to be read with the same value.
 *
 * rbw_mask: A '1' in this mask indicates that the corresponding bit (when not
 * being changed) should be written with the current value of that bit.  A '0'
 * in this mask indicates that the corresponding bit (when not being changed)
 * should be written with a value of '0'.
 */
static const struct {
	unsigned short address;         /* Address of the register */
	unsigned short constant_mask;	/* Constant modifiability mask */
	unsigned short rbw_mask;	/* Read-before-write mask */
} register_info_tbl[CPCAP_NUM_REG_CPCAP] = {
	[CPCAP_REG_INT1]      = {0, 0x0004, 0x0000},
	[CPCAP_REG_INT2]      = {1, 0x0000, 0x0000},
	[CPCAP_REG_INT3]      = {2, 0x0000, 0x0000},
	[CPCAP_REG_INT4]      = {3, 0xFC00, 0x0000},
	[CPCAP_REG_INTM1]     = {4, 0x0004, 0xFFFF},
	[CPCAP_REG_INTM2]     = {5, 0x0000, 0xFFFF},
	[CPCAP_REG_INTM3]     = {6, 0x0000, 0xFFFF},
	[CPCAP_REG_INTM4]     = {7, 0xFC00, 0xFFFF},
	[CPCAP_REG_INTS1]     = {8, 0xFFFF, 0xFFFF},
	[CPCAP_REG_INTS2]     = {9, 0xFFFF, 0xFFFF},
	[CPCAP_REG_INTS3]     = {10, 0xFFFF, 0xFFFF},
	[CPCAP_REG_INTS4]     = {11, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ASSIGN1]   = {12, 0x80F8, 0xFFFF},
	[CPCAP_REG_ASSIGN2]   = {13, 0x0000, 0xFFFF},
	[CPCAP_REG_ASSIGN3]   = {14, 0x0004, 0xFFFF},
	[CPCAP_REG_ASSIGN4]   = {15, 0x0068, 0xFFFF},
	[CPCAP_REG_ASSIGN5]   = {16, 0x0000, 0xFFFF},
	[CPCAP_REG_ASSIGN6]   = {17, 0xFC00, 0xFFFF},
	[CPCAP_REG_VERSC1]    = {18, 0xFFFF, 0xFFFF},
	[CPCAP_REG_VERSC2]    = {19, 0xFFFF, 0xFFFF},
	[CPCAP_REG_MI1]       = {128, 0x0000, 0x0000},
	[CPCAP_REG_MIM1]      = {129, 0x0000, 0xFFFF},
	[CPCAP_REG_MI2]       = {130, 0x0000, 0xFFFF},
	[CPCAP_REG_MIM2]      = {131, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UCC1]      = {132, 0xF000, 0xFFFF},
	[CPCAP_REG_UCC2]      = {133, 0xFC00, 0xFFFF},
	[CPCAP_REG_PC1]       = {135, 0xFC00, 0xFFFF},
	[CPCAP_REG_PC2]       = {136, 0xFC00, 0xFFFF},
	[CPCAP_REG_BPEOL]     = {137, 0xFE00, 0xFFFF},
	[CPCAP_REG_PGC]       = {138, 0xFE00, 0xFFFF},
	[CPCAP_REG_MT1]       = {139, 0x0000, 0x0000},
	[CPCAP_REG_MT2]       = {140, 0x0000, 0x0000},
	[CPCAP_REG_MT3]       = {141, 0x0000, 0x0000},
	[CPCAP_REG_PF]        = {142, 0x0000, 0xFFFF},
	[CPCAP_REG_SCC]       = {256, 0xFF00, 0xFFFF},
	[CPCAP_REG_SW1]       = {257, 0xFFFF, 0xFFFF},
	[CPCAP_REG_SW2]       = {258, 0xFC7F, 0xFFFF},
	[CPCAP_REG_UCTM]      = {259, 0xFFFE, 0xFFFF},
	[CPCAP_REG_TOD1]      = {260, 0xFF00, 0xFFFF},
	[CPCAP_REG_TOD2]      = {261, 0xFE00, 0xFFFF},
	[CPCAP_REG_TODA1]     = {262, 0xFF00, 0xFFFF},
	[CPCAP_REG_TODA2]     = {263, 0xFE00, 0xFFFF},
	[CPCAP_REG_DAY]       = {264, 0x8000, 0xFFFF},
	[CPCAP_REG_DAYA]      = {265, 0x8000, 0xFFFF},
	[CPCAP_REG_VAL1]      = {266, 0x0000, 0xFFFF},
	[CPCAP_REG_VAL2]      = {267, 0x0000, 0xFFFF},
	[CPCAP_REG_SDVSPLL]   = {384, 0x2488, 0xFFFF},
	[CPCAP_REG_SI2CC1]    = {385, 0x8000, 0xFFFF},
	[CPCAP_REG_Si2CC2]    = {386, 0xFF00, 0xFFFF},
	[CPCAP_REG_S1C1]      = {387, 0x9080, 0xFFFF},
	[CPCAP_REG_S1C2]      = {388, 0x8080, 0xFFFF},
	[CPCAP_REG_S2C1]      = {389, 0x9080, 0xFFFF},
	[CPCAP_REG_S2C2]      = {390, 0x8080, 0xFFFF},
	[CPCAP_REG_S3C]       = {391, 0xFA84, 0xFFFF},
	[CPCAP_REG_S4C1]      = {392, 0x9080, 0xFFFF},
	[CPCAP_REG_S4C2]      = {393, 0x8080, 0xFFFF},
	[CPCAP_REG_S5C]       = {394, 0xFFD5, 0xFFFF},
	[CPCAP_REG_S6C]       = {395, 0xFFF4, 0xFFFF},
	[CPCAP_REG_VCAMC]     = {396, 0xFF48, 0xFFFF},
	[CPCAP_REG_VCSIC]     = {397, 0xFFA8, 0xFFFF},
	[CPCAP_REG_VDACC]     = {398, 0xFF48, 0xFFFF},
	[CPCAP_REG_VDIGC]     = {399, 0xFF48, 0xFFFF},
	[CPCAP_REG_VFUSEC]    = {400, 0xFF50, 0xFFFF},
	[CPCAP_REG_VHVIOC]    = {401, 0xFFE8, 0xFFFF},
	[CPCAP_REG_VSDIOC]    = {402, 0xFF40, 0xFFFF},
	[CPCAP_REG_VPLLC]     = {403, 0xFFA4, 0xFFFF},
	[CPCAP_REG_VRF1C]     = {404, 0xFF50, 0xFFFF},
	[CPCAP_REG_VRF2C]     = {405, 0xFFD4, 0xFFFF},
	[CPCAP_REG_VRFREFC]   = {406, 0xFFD4, 0xFFFF},
	[CPCAP_REG_VWLAN1C]   = {407, 0xFFA8, 0xFFFF},
	[CPCAP_REG_VWLAN2C]   = {408, 0xFD32, 0xFFFF},
	[CPCAP_REG_VSIMC]     = {409, 0xE154, 0xFFFF},
	[CPCAP_REG_VVIBC]     = {410, 0xFFF2, 0xFFFF},
	[CPCAP_REG_VUSBC]     = {411, 0xFEA2, 0xFFFF},
	[CPCAP_REG_VUSBINT1C] = {412, 0xFFD4, 0xFFFF},
	[CPCAP_REG_VUSBINT2C] = {413, 0xFFD4, 0xFFFF},
	[CPCAP_REG_URT]       = {414, 0xFFFE, 0xFFFF},
	[CPCAP_REG_URM1]      = {415, 0x0000, 0xFFFF},
	[CPCAP_REG_URM2]      = {416, 0xFC00, 0xFFFF},
	[CPCAP_REG_VAUDIOC]   = {512, 0xFF88, 0xFFFF},
	[CPCAP_REG_CC]        = {513, 0x0000, 0xFEDF},
	[CPCAP_REG_CDI]       = {514, 0x4000, 0xFFFF},
	[CPCAP_REG_SDAC]      = {515, 0xF000, 0xFCFF},
	[CPCAP_REG_SDACDI]    = {516, 0xC000, 0xFFFF},
	[CPCAP_REG_TXI]       = {517, 0x0000, 0xFFFF},
	[CPCAP_REG_TXMP]      = {518, 0xF000, 0xFFFF},
	[CPCAP_REG_RXOA]      = {519, 0xF800, 0xFFFF},
	[CPCAP_REG_RXVC]      = {520, 0x00C3, 0xFFFF},
	[CPCAP_REG_RXCOA]     = {521, 0xF800, 0xFFFF},
	[CPCAP_REG_RXSDOA]    = {522, 0xE000, 0xFFFF},
	[CPCAP_REG_RXEPOA]    = {523, 0x8000, 0xFFFF},
	[CPCAP_REG_RXLL]      = {524, 0x0000, 0xFFFF},
	[CPCAP_REG_A2LA]      = {525, 0xFF00, 0xFFFF},
	[CPCAP_REG_MIPIS1]    = {526, 0x0000, 0xFFFF},
	[CPCAP_REG_MIPIS2]    = {527, 0xFF00, 0xFFFF},
	[CPCAP_REG_MIPIS3]    = {528, 0xFFFC, 0xFFFF},
	[CPCAP_REG_LVAB]      = {529, 0xFFFC, 0xFFFF},
	[CPCAP_REG_CCC1]      = {640, 0xFFF0, 0xFFFF},
	[CPCAP_REG_CRM]       = {641, 0xC000, 0xFFFF},
	[CPCAP_REG_CCCC2]     = {642, 0xFFC0, 0xFFFF},
	[CPCAP_REG_CCS1]      = {643, 0x0000, 0xFFFF},
	[CPCAP_REG_CCS2]      = {644, 0xFF00, 0xFFFF},
	[CPCAP_REG_CCA1]      = {645, 0x0000, 0xFFFF},
	[CPCAP_REG_CCA2]      = {646, 0x0000, 0xFFFF},
	[CPCAP_REG_CCM]       = {647, 0xFC00, 0xFFFF},
	[CPCAP_REG_CCO]       = {648, 0xFC00, 0xFFFF},
	[CPCAP_REG_CCI]       = {649, 0xC000, 0xFFFF},
	[CPCAP_REG_ADCC1]     = {768, 0x0000, 0xFFFF},
	[CPCAP_REG_ADCC2]     = {769, 0x0080, 0xFFFF},
	[CPCAP_REG_ADCD0]     = {770, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD1]     = {771, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD2]     = {772, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD3]     = {773, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD4]     = {774, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD5]     = {775, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD6]     = {776, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCD7]     = {777, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCAL1]    = {778, 0xFFFF, 0xFFFF},
	[CPCAP_REG_ADCAL2]    = {779, 0xFFFF, 0xFFFF},
	[CPCAP_REG_USBC1]     = {896, 0x0000, 0xFFFF},
	[CPCAP_REG_USBC2]     = {897, 0x0000, 0xFFFF},
	[CPCAP_REG_USBC3]     = {898, 0x8200, 0xFFFF},
	[CPCAP_REG_UVIDL]     = {899, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UVIDH]     = {900, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UPIDL]     = {901, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UPIDH]     = {902, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UFC1]      = {903, 0xFF80, 0xFFFF},
	[CPCAP_REG_UFC2]      = {904, 0xFF80, 0xFFFF},
	[CPCAP_REG_UFC3]      = {905, 0xFF80, 0xFFFF},
	[CPCAP_REG_UIC1]      = {906, 0xFF64, 0xFFFF},
	[CPCAP_REG_UIC2]      = {907, 0xFF64, 0xFFFF},
	[CPCAP_REG_UIC3]      = {908, 0xFF64, 0xFFFF},
	[CPCAP_REG_USBOTG1]   = {909, 0xFFC0, 0xFFFF},
	[CPCAP_REG_USBOTG2]   = {910, 0xFFC0, 0xFFFF},
	[CPCAP_REG_USBOTG3]   = {911, 0xFFC0, 0xFFFF},
	[CPCAP_REG_UIER1]     = {912, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIER2]     = {913, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIER3]     = {914, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIEF1]     = {915, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIEF2]     = {916, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIEF3]     = {917, 0xFFE0, 0xFFFF},
	[CPCAP_REG_UIS]       = {918, 0xFFFF, 0xFFFF},
	[CPCAP_REG_UIL]       = {919, 0xFFFF, 0xFFFF},
	[CPCAP_REG_USBD]      = {920, 0xFFFF, 0xFFFF},
	[CPCAP_REG_SCR1]      = {921, 0xFF00, 0xFFFF},
	[CPCAP_REG_SCR2]      = {922, 0xFF00, 0xFFFF},
	[CPCAP_REG_SCR3]      = {923, 0xFF00, 0xFFFF},
	[CPCAP_REG_VMC]       = {939, 0xFFFE, 0xFFFF},
	[CPCAP_REG_OWDC]      = {940, 0xFFFC, 0xFFFF},
	[CPCAP_REG_GPIO0]     = {941, 0x0D11, 0x3FFF},
	[CPCAP_REG_GPIO1]     = {943, 0x0D11, 0x3FFF},
	[CPCAP_REG_GPIO2]     = {945, 0x0D11, 0x3FFF},
	[CPCAP_REG_GPIO3]     = {947, 0x0D11, 0x3FFF},
	[CPCAP_REG_GPIO4]     = {949, 0x0D11, 0x3FFF},
	[CPCAP_REG_GPIO5]     = {951, 0x0C11, 0x3FFF},
	[CPCAP_REG_GPIO6]     = {953, 0x0C11, 0x3FFF},
	[CPCAP_REG_MDLC]      = {1024, 0x0000, 0xFFFF},
	[CPCAP_REG_KLC]       = {1025, 0x8000, 0xFFFF},
	[CPCAP_REG_ADLC]      = {1026, 0x8000, 0xFFFF},
	[CPCAP_REG_REDC]      = {1027, 0xFC00, 0xFFFF},
	[CPCAP_REG_GREENC]    = {1028, 0xFC00, 0xFFFF},
	[CPCAP_REG_BLUEC]     = {1029, 0xFC00, 0xFFFF},
	[CPCAP_REG_CFC]       = {1030, 0xF000, 0xFFFF},
	[CPCAP_REG_ABC]       = {1031, 0xFFC3, 0xFFFF},
	[CPCAP_REG_BLEDC]     = {1032, 0xFC00, 0xFFFF},
	[CPCAP_REG_CLEDC]     = {1033, 0xFC00, 0xFFFF},
	[CPCAP_REG_OW1C]      = {1152, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW1D]      = {1153, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW1I]      = {1154, 0xFFFF, 0xFFFF},
	[CPCAP_REG_OW1IE]     = {1155, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW1]       = {1157, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW2C]      = {1160, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW2D]      = {1161, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW2I]      = {1162, 0xFFFF, 0xFFFF},
	[CPCAP_REG_OW2IE]     = {1163, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW2]       = {1165, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW3C]      = {1168, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW3D]      = {1169, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW3I]      = {1170, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW3IE]     = {1171, 0xFF00, 0xFFFF},
	[CPCAP_REG_OW3]       = {1173, 0xFF00, 0xFFFF},
	[CPCAP_REG_GCAIC]     = {1174, 0xFF00, 0xFFFF},
	[CPCAP_REG_GCAIM]     = {1175, 0xFF00, 0xFFFF},
	[CPCAP_REG_LGDIR]     = {1176, 0xFFE0, 0xFFFF},
	[CPCAP_REG_LGPU]      = {1177, 0xFFE0, 0xFFFF},
	[CPCAP_REG_LGPIN]     = {1178, 0xFF00, 0xFFFF},
	[CPCAP_REG_LGMASK]    = {1179, 0xFFE0, 0xFFFF},
	[CPCAP_REG_LDEB]      = {1180, 0xFF00, 0xFFFF},
	[CPCAP_REG_LGDET]     = {1181, 0xFF00, 0xFFFF},
	[CPCAP_REG_LMISC]     = {1182, 0xFF07, 0xFFFF},
	[CPCAP_REG_LMACE]     = {1183, 0xFFF8, 0xFFFF},
};

static int cpcap_spi_access(struct spi_device *spi, u8 *buf,
			    size_t len)
{
	struct spi_message m;
	struct spi_transfer t = {
		.tx_buf = buf,
		.len = len,
		.rx_buf = buf,
		.bits_per_word = 32,
	};

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static int cpcap_config_for_read(struct spi_device *spi, unsigned short reg,
				 unsigned short *data)
{
	int status = -ENOTTY;
	u32 buf32;  /* force buf to be 32bit aligned */
	u8 *buf = (u8 *) &buf32;

	if (spi != NULL) {
		buf[3] = (reg >> 6) & 0x000000FF;
		buf[2] = (reg << 2) & 0x000000FF;
		buf[1] = 0;
		buf[0] = 0;

		status = cpcap_spi_access(spi, buf, 4);

		if (status == 0)
			*data = buf[0] | (buf[1] << 8);
	}

	return status;
}

static int cpcap_config_for_write(struct spi_device *spi, unsigned short reg,
				  unsigned short data)
{
	int status = -ENOTTY;
	u32 buf32;  /* force buf to be 32bit aligned */
	u8 *buf = (u8 *) &buf32;

	if (spi != NULL) {
		buf[3] = ((reg >> 6) & 0x000000FF) | 0x80;
		buf[2] = (reg << 2) & 0x000000FF;
		buf[1] = (data >> 8) & 0x000000FF;
		buf[0] = data & 0x000000FF;

		status = cpcap_spi_access(spi, buf, 4);
	}

	return status;
}

int cpcap_regacc_read(struct cpcap_device *cpcap, enum cpcap_reg reg,
		      unsigned short *value_ptr)
{
	int retval = -EINVAL;
	struct spi_device *spi = cpcap->spi;

	if (IS_CPCAP(reg) && (value_ptr != 0)) {
		mutex_lock(&reg_access);

		retval = cpcap_config_for_read(spi, register_info_tbl
				      [reg].address, value_ptr);

		mutex_unlock(&reg_access);
	}

	return retval;
}

int cpcap_regacc_write(struct cpcap_device *cpcap,
		       enum cpcap_reg reg,
		       unsigned short value,
		       unsigned short mask)
{
	int retval = -EINVAL;
	unsigned short old_value = 0;
	struct cpcap_platform_data *data;
	struct spi_device *spi = cpcap->spi;

	data = (struct cpcap_platform_data *)spi->controller_data;

	if (IS_CPCAP(reg) &&
	    (mask & register_info_tbl[reg].constant_mask) == 0) {
		mutex_lock(&reg_access);

		value &= mask;

		if ((register_info_tbl[reg].rbw_mask) != 0) {
			retval = cpcap_config_for_read(spi, register_info_tbl
						       [reg].address,
						       &old_value);
			if (retval != 0)
				goto error;
		}

		old_value &= register_info_tbl[reg].rbw_mask;
		old_value &= ~mask;
		value |= old_value;
		retval = cpcap_config_for_write(spi,
						register_info_tbl[reg].address,
						value);
error:
		mutex_unlock(&reg_access);
	}

	return retval;
}

int cpcap_regacc_init(struct cpcap_device *cpcap)
{
	unsigned short i;
	unsigned short mask;
	int retval = 0;
	struct cpcap_platform_data *data;
	struct spi_device *spi = cpcap->spi;

	data = (struct cpcap_platform_data *)spi->controller_data;

	for (i = 0; i < data->init_len; i++) {
		mask = 0xFFFF;
		mask &= ~(register_info_tbl[data->init[i].reg].constant_mask);

		retval = cpcap_regacc_write(cpcap, data->init[i].reg,
					    data->init[i].data,
					    mask);
		if (retval)
			break;
	}

	return retval;
}
