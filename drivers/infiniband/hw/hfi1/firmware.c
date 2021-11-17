// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
 */

#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/crc32.h>

#include "hfi.h"
#include "trace.h"

/*
 * Make it easy to toggle firmware file name and if it gets loaded by
 * editing the following. This may be something we do while in development
 * but not necessarily something a user would ever need to use.
 */
#define DEFAULT_FW_8051_NAME_FPGA "hfi_dc8051.bin"
#define DEFAULT_FW_8051_NAME_ASIC "hfi1_dc8051.fw"
#define DEFAULT_FW_FABRIC_NAME "hfi1_fabric.fw"
#define DEFAULT_FW_SBUS_NAME "hfi1_sbus.fw"
#define DEFAULT_FW_PCIE_NAME "hfi1_pcie.fw"
#define ALT_FW_8051_NAME_ASIC "hfi1_dc8051_d.fw"
#define ALT_FW_FABRIC_NAME "hfi1_fabric_d.fw"
#define ALT_FW_SBUS_NAME "hfi1_sbus_d.fw"
#define ALT_FW_PCIE_NAME "hfi1_pcie_d.fw"

MODULE_FIRMWARE(DEFAULT_FW_8051_NAME_ASIC);
MODULE_FIRMWARE(DEFAULT_FW_FABRIC_NAME);
MODULE_FIRMWARE(DEFAULT_FW_SBUS_NAME);
MODULE_FIRMWARE(DEFAULT_FW_PCIE_NAME);

static uint fw_8051_load = 1;
static uint fw_fabric_serdes_load = 1;
static uint fw_pcie_serdes_load = 1;
static uint fw_sbus_load = 1;

/* Firmware file names get set in hfi1_firmware_init() based on the above */
static char *fw_8051_name;
static char *fw_fabric_serdes_name;
static char *fw_sbus_name;
static char *fw_pcie_serdes_name;

#define SBUS_MAX_POLL_COUNT 100
#define SBUS_COUNTER(reg, name) \
	(((reg) >> ASIC_STS_SBUS_COUNTERS_##name##_CNT_SHIFT) & \
	 ASIC_STS_SBUS_COUNTERS_##name##_CNT_MASK)

/*
 * Firmware security header.
 */
struct css_header {
	u32 module_type;
	u32 header_len;
	u32 header_version;
	u32 module_id;
	u32 module_vendor;
	u32 date;		/* BCD yyyymmdd */
	u32 size;		/* in DWORDs */
	u32 key_size;		/* in DWORDs */
	u32 modulus_size;	/* in DWORDs */
	u32 exponent_size;	/* in DWORDs */
	u32 reserved[22];
};

/* expected field values */
#define CSS_MODULE_TYPE	   0x00000006
#define CSS_HEADER_LEN	   0x000000a1
#define CSS_HEADER_VERSION 0x00010000
#define CSS_MODULE_VENDOR  0x00008086

#define KEY_SIZE      256
#define MU_SIZE		8
#define EXPONENT_SIZE	4

/* size of platform configuration partition */
#define MAX_PLATFORM_CONFIG_FILE_SIZE 4096

/* size of file of plaform configuration encoded in format version 4 */
#define PLATFORM_CONFIG_FORMAT_4_FILE_SIZE 528

/* the file itself */
struct firmware_file {
	struct css_header css_header;
	u8 modulus[KEY_SIZE];
	u8 exponent[EXPONENT_SIZE];
	u8 signature[KEY_SIZE];
	u8 firmware[];
};

struct augmented_firmware_file {
	struct css_header css_header;
	u8 modulus[KEY_SIZE];
	u8 exponent[EXPONENT_SIZE];
	u8 signature[KEY_SIZE];
	u8 r2[KEY_SIZE];
	u8 mu[MU_SIZE];
	u8 firmware[];
};

/* augmented file size difference */
#define AUGMENT_SIZE (sizeof(struct augmented_firmware_file) - \
						sizeof(struct firmware_file))

struct firmware_details {
	/* Linux core piece */
	const struct firmware *fw;

	struct css_header *css_header;
	u8 *firmware_ptr;		/* pointer to binary data */
	u32 firmware_len;		/* length in bytes */
	u8 *modulus;			/* pointer to the modulus */
	u8 *exponent;			/* pointer to the exponent */
	u8 *signature;			/* pointer to the signature */
	u8 *r2;				/* pointer to r2 */
	u8 *mu;				/* pointer to mu */
	struct augmented_firmware_file dummy_header;
};

/*
 * The mutex protects fw_state, fw_err, and all of the firmware_details
 * variables.
 */
static DEFINE_MUTEX(fw_mutex);
enum fw_state {
	FW_EMPTY,
	FW_TRY,
	FW_FINAL,
	FW_ERR
};

static enum fw_state fw_state = FW_EMPTY;
static int fw_err;
static struct firmware_details fw_8051;
static struct firmware_details fw_fabric;
static struct firmware_details fw_pcie;
static struct firmware_details fw_sbus;

/* flags for turn_off_spicos() */
#define SPICO_SBUS   0x1
#define SPICO_FABRIC 0x2
#define ENABLE_SPICO_SMASK 0x1

/* security block commands */
#define RSA_CMD_INIT  0x1
#define RSA_CMD_START 0x2

/* security block status */
#define RSA_STATUS_IDLE   0x0
#define RSA_STATUS_ACTIVE 0x1
#define RSA_STATUS_DONE   0x2
#define RSA_STATUS_FAILED 0x3

/* RSA engine timeout, in ms */
#define RSA_ENGINE_TIMEOUT 100 /* ms */

/* hardware mutex timeout, in ms */
#define HM_TIMEOUT 10 /* ms */

/* 8051 memory access timeout, in us */
#define DC8051_ACCESS_TIMEOUT 100 /* us */

/* the number of fabric SerDes on the SBus */
#define NUM_FABRIC_SERDES 4

/* ASIC_STS_SBUS_RESULT.RESULT_CODE value */
#define SBUS_READ_COMPLETE 0x4

/* SBus fabric SerDes addresses, one set per HFI */
static const u8 fabric_serdes_addrs[2][NUM_FABRIC_SERDES] = {
	{ 0x01, 0x02, 0x03, 0x04 },
	{ 0x28, 0x29, 0x2a, 0x2b }
};

/* SBus PCIe SerDes addresses, one set per HFI */
static const u8 pcie_serdes_addrs[2][NUM_PCIE_SERDES] = {
	{ 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16,
	  0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22, 0x24, 0x26 },
	{ 0x2f, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3b, 0x3d,
	  0x3f, 0x41, 0x43, 0x45, 0x47, 0x49, 0x4b, 0x4d }
};

/* SBus PCIe PCS addresses, one set per HFI */
const u8 pcie_pcs_addrs[2][NUM_PCIE_SERDES] = {
	{ 0x09, 0x0b, 0x0d, 0x0f, 0x11, 0x13, 0x15, 0x17,
	  0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25, 0x27 },
	{ 0x30, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3c, 0x3e,
	  0x40, 0x42, 0x44, 0x46, 0x48, 0x4a, 0x4c, 0x4e }
};

/* SBus fabric SerDes broadcast addresses, one per HFI */
static const u8 fabric_serdes_broadcast[2] = { 0xe4, 0xe5 };
static const u8 all_fabric_serdes_broadcast = 0xe1;

/* SBus PCIe SerDes broadcast addresses, one per HFI */
const u8 pcie_serdes_broadcast[2] = { 0xe2, 0xe3 };
static const u8 all_pcie_serdes_broadcast = 0xe0;

static const u32 platform_config_table_limits[PLATFORM_CONFIG_TABLE_MAX] = {
	0,
	SYSTEM_TABLE_MAX,
	PORT_TABLE_MAX,
	RX_PRESET_TABLE_MAX,
	TX_PRESET_TABLE_MAX,
	QSFP_ATTEN_TABLE_MAX,
	VARIABLE_SETTINGS_TABLE_MAX
};

/* forwards */
static void dispose_one_firmware(struct firmware_details *fdet);
static int load_fabric_serdes_firmware(struct hfi1_devdata *dd,
				       struct firmware_details *fdet);
static void dump_fw_version(struct hfi1_devdata *dd);

/*
 * Read a single 64-bit value from 8051 data memory.
 *
 * Expects:
 * o caller to have already set up data read, no auto increment
 * o caller to turn off read enable when finished
 *
 * The address argument is a byte offset.  Bits 0:2 in the address are
 * ignored - i.e. the hardware will always do aligned 8-byte reads as if
 * the lower bits are zero.
 *
 * Return 0 on success, -ENXIO on a read error (timeout).
 */
static int __read_8051_data(struct hfi1_devdata *dd, u32 addr, u64 *result)
{
	u64 reg;
	int count;

	/* step 1: set the address, clear enable */
	reg = (addr & DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_MASK)
			<< DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_SHIFT;
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_CTRL, reg);
	/* step 2: enable */
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_CTRL,
		  reg | DC_DC8051_CFG_RAM_ACCESS_CTRL_READ_ENA_SMASK);

	/* wait until ACCESS_COMPLETED is set */
	count = 0;
	while ((read_csr(dd, DC_DC8051_CFG_RAM_ACCESS_STATUS)
		    & DC_DC8051_CFG_RAM_ACCESS_STATUS_ACCESS_COMPLETED_SMASK)
		    == 0) {
		count++;
		if (count > DC8051_ACCESS_TIMEOUT) {
			dd_dev_err(dd, "timeout reading 8051 data\n");
			return -ENXIO;
		}
		ndelay(10);
	}

	/* gather the data */
	*result = read_csr(dd, DC_DC8051_CFG_RAM_ACCESS_RD_DATA);

	return 0;
}

/*
 * Read 8051 data starting at addr, for len bytes.  Will read in 8-byte chunks.
 * Return 0 on success, -errno on error.
 */
int read_8051_data(struct hfi1_devdata *dd, u32 addr, u32 len, u64 *result)
{
	unsigned long flags;
	u32 done;
	int ret = 0;

	spin_lock_irqsave(&dd->dc8051_memlock, flags);

	/* data read set-up, no auto-increment */
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_SETUP, 0);

	for (done = 0; done < len; addr += 8, done += 8, result++) {
		ret = __read_8051_data(dd, addr, result);
		if (ret)
			break;
	}

	/* turn off read enable */
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_CTRL, 0);

	spin_unlock_irqrestore(&dd->dc8051_memlock, flags);

	return ret;
}

/*
 * Write data or code to the 8051 code or data RAM.
 */
static int write_8051(struct hfi1_devdata *dd, int code, u32 start,
		      const u8 *data, u32 len)
{
	u64 reg;
	u32 offset;
	int aligned, count;

	/* check alignment */
	aligned = ((unsigned long)data & 0x7) == 0;

	/* write set-up */
	reg = (code ? DC_DC8051_CFG_RAM_ACCESS_SETUP_RAM_SEL_SMASK : 0ull)
		| DC_DC8051_CFG_RAM_ACCESS_SETUP_AUTO_INCR_ADDR_SMASK;
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_SETUP, reg);

	reg = ((start & DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_MASK)
			<< DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_SHIFT)
		| DC_DC8051_CFG_RAM_ACCESS_CTRL_WRITE_ENA_SMASK;
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_CTRL, reg);

	/* write */
	for (offset = 0; offset < len; offset += 8) {
		int bytes = len - offset;

		if (bytes < 8) {
			reg = 0;
			memcpy(&reg, &data[offset], bytes);
		} else if (aligned) {
			reg = *(u64 *)&data[offset];
		} else {
			memcpy(&reg, &data[offset], 8);
		}
		write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_WR_DATA, reg);

		/* wait until ACCESS_COMPLETED is set */
		count = 0;
		while ((read_csr(dd, DC_DC8051_CFG_RAM_ACCESS_STATUS)
		    & DC_DC8051_CFG_RAM_ACCESS_STATUS_ACCESS_COMPLETED_SMASK)
		    == 0) {
			count++;
			if (count > DC8051_ACCESS_TIMEOUT) {
				dd_dev_err(dd, "timeout writing 8051 data\n");
				return -ENXIO;
			}
			udelay(1);
		}
	}

	/* turn off write access, auto increment (also sets to data access) */
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_CTRL, 0);
	write_csr(dd, DC_DC8051_CFG_RAM_ACCESS_SETUP, 0);

	return 0;
}

/* return 0 if values match, non-zero and complain otherwise */
static int invalid_header(struct hfi1_devdata *dd, const char *what,
			  u32 actual, u32 expected)
{
	if (actual == expected)
		return 0;

	dd_dev_err(dd,
		   "invalid firmware header field %s: expected 0x%x, actual 0x%x\n",
		   what, expected, actual);
	return 1;
}

/*
 * Verify that the static fields in the CSS header match.
 */
static int verify_css_header(struct hfi1_devdata *dd, struct css_header *css)
{
	/* verify CSS header fields (most sizes are in DW, so add /4) */
	if (invalid_header(dd, "module_type", css->module_type,
			   CSS_MODULE_TYPE) ||
	    invalid_header(dd, "header_len", css->header_len,
			   (sizeof(struct firmware_file) / 4)) ||
	    invalid_header(dd, "header_version", css->header_version,
			   CSS_HEADER_VERSION) ||
	    invalid_header(dd, "module_vendor", css->module_vendor,
			   CSS_MODULE_VENDOR) ||
	    invalid_header(dd, "key_size", css->key_size, KEY_SIZE / 4) ||
	    invalid_header(dd, "modulus_size", css->modulus_size,
			   KEY_SIZE / 4) ||
	    invalid_header(dd, "exponent_size", css->exponent_size,
			   EXPONENT_SIZE / 4)) {
		return -EINVAL;
	}
	return 0;
}

/*
 * Make sure there are at least some bytes after the prefix.
 */
static int payload_check(struct hfi1_devdata *dd, const char *name,
			 long file_size, long prefix_size)
{
	/* make sure we have some payload */
	if (prefix_size >= file_size) {
		dd_dev_err(dd,
			   "firmware \"%s\", size %ld, must be larger than %ld bytes\n",
			   name, file_size, prefix_size);
		return -EINVAL;
	}

	return 0;
}

/*
 * Request the firmware from the system.  Extract the pieces and fill in
 * fdet.  If successful, the caller will need to call dispose_one_firmware().
 * Returns 0 on success, -ERRNO on error.
 */
static int obtain_one_firmware(struct hfi1_devdata *dd, const char *name,
			       struct firmware_details *fdet)
{
	struct css_header *css;
	int ret;

	memset(fdet, 0, sizeof(*fdet));

	ret = request_firmware(&fdet->fw, name, &dd->pcidev->dev);
	if (ret) {
		dd_dev_warn(dd, "cannot find firmware \"%s\", err %d\n",
			    name, ret);
		return ret;
	}

	/* verify the firmware */
	if (fdet->fw->size < sizeof(struct css_header)) {
		dd_dev_err(dd, "firmware \"%s\" is too small\n", name);
		ret = -EINVAL;
		goto done;
	}
	css = (struct css_header *)fdet->fw->data;

	hfi1_cdbg(FIRMWARE, "Firmware %s details:", name);
	hfi1_cdbg(FIRMWARE, "file size: 0x%lx bytes", fdet->fw->size);
	hfi1_cdbg(FIRMWARE, "CSS structure:");
	hfi1_cdbg(FIRMWARE, "  module_type    0x%x", css->module_type);
	hfi1_cdbg(FIRMWARE, "  header_len     0x%03x (0x%03x bytes)",
		  css->header_len, 4 * css->header_len);
	hfi1_cdbg(FIRMWARE, "  header_version 0x%x", css->header_version);
	hfi1_cdbg(FIRMWARE, "  module_id      0x%x", css->module_id);
	hfi1_cdbg(FIRMWARE, "  module_vendor  0x%x", css->module_vendor);
	hfi1_cdbg(FIRMWARE, "  date           0x%x", css->date);
	hfi1_cdbg(FIRMWARE, "  size           0x%03x (0x%03x bytes)",
		  css->size, 4 * css->size);
	hfi1_cdbg(FIRMWARE, "  key_size       0x%03x (0x%03x bytes)",
		  css->key_size, 4 * css->key_size);
	hfi1_cdbg(FIRMWARE, "  modulus_size   0x%03x (0x%03x bytes)",
		  css->modulus_size, 4 * css->modulus_size);
	hfi1_cdbg(FIRMWARE, "  exponent_size  0x%03x (0x%03x bytes)",
		  css->exponent_size, 4 * css->exponent_size);
	hfi1_cdbg(FIRMWARE, "firmware size: 0x%lx bytes",
		  fdet->fw->size - sizeof(struct firmware_file));

	/*
	 * If the file does not have a valid CSS header, fail.
	 * Otherwise, check the CSS size field for an expected size.
	 * The augmented file has r2 and mu inserted after the header
	 * was generated, so there will be a known difference between
	 * the CSS header size and the actual file size.  Use this
	 * difference to identify an augmented file.
	 *
	 * Note: css->size is in DWORDs, multiply by 4 to get bytes.
	 */
	ret = verify_css_header(dd, css);
	if (ret) {
		dd_dev_info(dd, "Invalid CSS header for \"%s\"\n", name);
	} else if ((css->size * 4) == fdet->fw->size) {
		/* non-augmented firmware file */
		struct firmware_file *ff = (struct firmware_file *)
							fdet->fw->data;

		/* make sure there are bytes in the payload */
		ret = payload_check(dd, name, fdet->fw->size,
				    sizeof(struct firmware_file));
		if (ret == 0) {
			fdet->css_header = css;
			fdet->modulus = ff->modulus;
			fdet->exponent = ff->exponent;
			fdet->signature = ff->signature;
			fdet->r2 = fdet->dummy_header.r2; /* use dummy space */
			fdet->mu = fdet->dummy_header.mu; /* use dummy space */
			fdet->firmware_ptr = ff->firmware;
			fdet->firmware_len = fdet->fw->size -
						sizeof(struct firmware_file);
			/*
			 * Header does not include r2 and mu - generate here.
			 * For now, fail.
			 */
			dd_dev_err(dd, "driver is unable to validate firmware without r2 and mu (not in firmware file)\n");
			ret = -EINVAL;
		}
	} else if ((css->size * 4) + AUGMENT_SIZE == fdet->fw->size) {
		/* augmented firmware file */
		struct augmented_firmware_file *aff =
			(struct augmented_firmware_file *)fdet->fw->data;

		/* make sure there are bytes in the payload */
		ret = payload_check(dd, name, fdet->fw->size,
				    sizeof(struct augmented_firmware_file));
		if (ret == 0) {
			fdet->css_header = css;
			fdet->modulus = aff->modulus;
			fdet->exponent = aff->exponent;
			fdet->signature = aff->signature;
			fdet->r2 = aff->r2;
			fdet->mu = aff->mu;
			fdet->firmware_ptr = aff->firmware;
			fdet->firmware_len = fdet->fw->size -
					sizeof(struct augmented_firmware_file);
		}
	} else {
		/* css->size check failed */
		dd_dev_err(dd,
			   "invalid firmware header field size: expected 0x%lx or 0x%lx, actual 0x%x\n",
			   fdet->fw->size / 4,
			   (fdet->fw->size - AUGMENT_SIZE) / 4,
			   css->size);

		ret = -EINVAL;
	}

done:
	/* if returning an error, clean up after ourselves */
	if (ret)
		dispose_one_firmware(fdet);
	return ret;
}

static void dispose_one_firmware(struct firmware_details *fdet)
{
	release_firmware(fdet->fw);
	/* erase all previous information */
	memset(fdet, 0, sizeof(*fdet));
}

/*
 * Obtain the 4 firmwares from the OS.  All must be obtained at once or not
 * at all.  If called with the firmware state in FW_TRY, use alternate names.
 * On exit, this routine will have set the firmware state to one of FW_TRY,
 * FW_FINAL, or FW_ERR.
 *
 * Must be holding fw_mutex.
 */
static void __obtain_firmware(struct hfi1_devdata *dd)
{
	int err = 0;

	if (fw_state == FW_FINAL)	/* nothing more to obtain */
		return;
	if (fw_state == FW_ERR)		/* already in error */
		return;

	/* fw_state is FW_EMPTY or FW_TRY */
retry:
	if (fw_state == FW_TRY) {
		/*
		 * We tried the original and it failed.  Move to the
		 * alternate.
		 */
		dd_dev_warn(dd, "using alternate firmware names\n");
		/*
		 * Let others run.  Some systems, when missing firmware, does
		 * something that holds for 30 seconds.  If we do that twice
		 * in a row it triggers task blocked warning.
		 */
		cond_resched();
		if (fw_8051_load)
			dispose_one_firmware(&fw_8051);
		if (fw_fabric_serdes_load)
			dispose_one_firmware(&fw_fabric);
		if (fw_sbus_load)
			dispose_one_firmware(&fw_sbus);
		if (fw_pcie_serdes_load)
			dispose_one_firmware(&fw_pcie);
		fw_8051_name = ALT_FW_8051_NAME_ASIC;
		fw_fabric_serdes_name = ALT_FW_FABRIC_NAME;
		fw_sbus_name = ALT_FW_SBUS_NAME;
		fw_pcie_serdes_name = ALT_FW_PCIE_NAME;

		/*
		 * Add a delay before obtaining and loading debug firmware.
		 * Authorization will fail if the delay between firmware
		 * authorization events is shorter than 50us. Add 100us to
		 * make a delay time safe.
		 */
		usleep_range(100, 120);
	}

	if (fw_sbus_load) {
		err = obtain_one_firmware(dd, fw_sbus_name, &fw_sbus);
		if (err)
			goto done;
	}

	if (fw_pcie_serdes_load) {
		err = obtain_one_firmware(dd, fw_pcie_serdes_name, &fw_pcie);
		if (err)
			goto done;
	}

	if (fw_fabric_serdes_load) {
		err = obtain_one_firmware(dd, fw_fabric_serdes_name,
					  &fw_fabric);
		if (err)
			goto done;
	}

	if (fw_8051_load) {
		err = obtain_one_firmware(dd, fw_8051_name, &fw_8051);
		if (err)
			goto done;
	}

done:
	if (err) {
		/* oops, had problems obtaining a firmware */
		if (fw_state == FW_EMPTY && dd->icode == ICODE_RTL_SILICON) {
			/* retry with alternate (RTL only) */
			fw_state = FW_TRY;
			goto retry;
		}
		dd_dev_err(dd, "unable to obtain working firmware\n");
		fw_state = FW_ERR;
		fw_err = -ENOENT;
	} else {
		/* success */
		if (fw_state == FW_EMPTY &&
		    dd->icode != ICODE_FUNCTIONAL_SIMULATOR)
			fw_state = FW_TRY;	/* may retry later */
		else
			fw_state = FW_FINAL;	/* cannot try again */
	}
}

/*
 * Called by all HFIs when loading their firmware - i.e. device probe time.
 * The first one will do the actual firmware load.  Use a mutex to resolve
 * any possible race condition.
 *
 * The call to this routine cannot be moved to driver load because the kernel
 * call request_firmware() requires a device which is only available after
 * the first device probe.
 */
static int obtain_firmware(struct hfi1_devdata *dd)
{
	unsigned long timeout;

	mutex_lock(&fw_mutex);

	/* 40s delay due to long delay on missing firmware on some systems */
	timeout = jiffies + msecs_to_jiffies(40000);
	while (fw_state == FW_TRY) {
		/*
		 * Another device is trying the firmware.  Wait until it
		 * decides what works (or not).
		 */
		if (time_after(jiffies, timeout)) {
			/* waited too long */
			dd_dev_err(dd, "Timeout waiting for firmware try");
			fw_state = FW_ERR;
			fw_err = -ETIMEDOUT;
			break;
		}
		mutex_unlock(&fw_mutex);
		msleep(20);	/* arbitrary delay */
		mutex_lock(&fw_mutex);
	}
	/* not in FW_TRY state */

	/* set fw_state to FW_TRY, FW_FINAL, or FW_ERR, and fw_err */
	if (fw_state == FW_EMPTY)
		__obtain_firmware(dd);

	mutex_unlock(&fw_mutex);
	return fw_err;
}

/*
 * Called when the driver unloads.  The timing is asymmetric with its
 * counterpart, obtain_firmware().  If called at device remove time,
 * then it is conceivable that another device could probe while the
 * firmware is being disposed.  The mutexes can be moved to do that
 * safely, but then the firmware would be requested from the OS multiple
 * times.
 *
 * No mutex is needed as the driver is unloading and there cannot be any
 * other callers.
 */
void dispose_firmware(void)
{
	dispose_one_firmware(&fw_8051);
	dispose_one_firmware(&fw_fabric);
	dispose_one_firmware(&fw_pcie);
	dispose_one_firmware(&fw_sbus);

	/* retain the error state, otherwise revert to empty */
	if (fw_state != FW_ERR)
		fw_state = FW_EMPTY;
}

/*
 * Called with the result of a firmware download.
 *
 * Return 1 to retry loading the firmware, 0 to stop.
 */
static int retry_firmware(struct hfi1_devdata *dd, int load_result)
{
	int retry;

	mutex_lock(&fw_mutex);

	if (load_result == 0) {
		/*
		 * The load succeeded, so expect all others to do the same.
		 * Do not retry again.
		 */
		if (fw_state == FW_TRY)
			fw_state = FW_FINAL;
		retry = 0;	/* do NOT retry */
	} else if (fw_state == FW_TRY) {
		/* load failed, obtain alternate firmware */
		__obtain_firmware(dd);
		retry = (fw_state == FW_FINAL);
	} else {
		/* else in FW_FINAL or FW_ERR, no retry in either case */
		retry = 0;
	}

	mutex_unlock(&fw_mutex);
	return retry;
}

/*
 * Write a block of data to a given array CSR.  All calls will be in
 * multiples of 8 bytes.
 */
static void write_rsa_data(struct hfi1_devdata *dd, int what,
			   const u8 *data, int nbytes)
{
	int qw_size = nbytes / 8;
	int i;

	if (((unsigned long)data & 0x7) == 0) {
		/* aligned */
		u64 *ptr = (u64 *)data;

		for (i = 0; i < qw_size; i++, ptr++)
			write_csr(dd, what + (8 * i), *ptr);
	} else {
		/* not aligned */
		for (i = 0; i < qw_size; i++, data += 8) {
			u64 value;

			memcpy(&value, data, 8);
			write_csr(dd, what + (8 * i), value);
		}
	}
}

/*
 * Write a block of data to a given CSR as a stream of writes.  All calls will
 * be in multiples of 8 bytes.
 */
static void write_streamed_rsa_data(struct hfi1_devdata *dd, int what,
				    const u8 *data, int nbytes)
{
	u64 *ptr = (u64 *)data;
	int qw_size = nbytes / 8;

	for (; qw_size > 0; qw_size--, ptr++)
		write_csr(dd, what, *ptr);
}

/*
 * Download the signature and start the RSA mechanism.  Wait for
 * RSA_ENGINE_TIMEOUT before giving up.
 */
static int run_rsa(struct hfi1_devdata *dd, const char *who,
		   const u8 *signature)
{
	unsigned long timeout;
	u64 reg;
	u32 status;
	int ret = 0;

	/* write the signature */
	write_rsa_data(dd, MISC_CFG_RSA_SIGNATURE, signature, KEY_SIZE);

	/* initialize RSA */
	write_csr(dd, MISC_CFG_RSA_CMD, RSA_CMD_INIT);

	/*
	 * Make sure the engine is idle and insert a delay between the two
	 * writes to MISC_CFG_RSA_CMD.
	 */
	status = (read_csr(dd, MISC_CFG_FW_CTRL)
			   & MISC_CFG_FW_CTRL_RSA_STATUS_SMASK)
			     >> MISC_CFG_FW_CTRL_RSA_STATUS_SHIFT;
	if (status != RSA_STATUS_IDLE) {
		dd_dev_err(dd, "%s security engine not idle - giving up\n",
			   who);
		return -EBUSY;
	}

	/* start RSA */
	write_csr(dd, MISC_CFG_RSA_CMD, RSA_CMD_START);

	/*
	 * Look for the result.
	 *
	 * The RSA engine is hooked up to two MISC errors.  The driver
	 * masks these errors as they do not respond to the standard
	 * error "clear down" mechanism.  Look for these errors here and
	 * clear them when possible.  This routine will exit with the
	 * errors of the current run still set.
	 *
	 * MISC_FW_AUTH_FAILED_ERR
	 *	Firmware authorization failed.  This can be cleared by
	 *	re-initializing the RSA engine, then clearing the status bit.
	 *	Do not re-init the RSA angine immediately after a successful
	 *	run - this will reset the current authorization.
	 *
	 * MISC_KEY_MISMATCH_ERR
	 *	Key does not match.  The only way to clear this is to load
	 *	a matching key then clear the status bit.  If this error
	 *	is raised, it will persist outside of this routine until a
	 *	matching key is loaded.
	 */
	timeout = msecs_to_jiffies(RSA_ENGINE_TIMEOUT) + jiffies;
	while (1) {
		status = (read_csr(dd, MISC_CFG_FW_CTRL)
			   & MISC_CFG_FW_CTRL_RSA_STATUS_SMASK)
			     >> MISC_CFG_FW_CTRL_RSA_STATUS_SHIFT;

		if (status == RSA_STATUS_IDLE) {
			/* should not happen */
			dd_dev_err(dd, "%s firmware security bad idle state\n",
				   who);
			ret = -EINVAL;
			break;
		} else if (status == RSA_STATUS_DONE) {
			/* finished successfully */
			break;
		} else if (status == RSA_STATUS_FAILED) {
			/* finished unsuccessfully */
			ret = -EINVAL;
			break;
		}
		/* else still active */

		if (time_after(jiffies, timeout)) {
			/*
			 * Timed out while active.  We can't reset the engine
			 * if it is stuck active, but run through the
			 * error code to see what error bits are set.
			 */
			dd_dev_err(dd, "%s firmware security time out\n", who);
			ret = -ETIMEDOUT;
			break;
		}

		msleep(20);
	}

	/*
	 * Arrive here on success or failure.  Clear all RSA engine
	 * errors.  All current errors will stick - the RSA logic is keeping
	 * error high.  All previous errors will clear - the RSA logic
	 * is not keeping the error high.
	 */
	write_csr(dd, MISC_ERR_CLEAR,
		  MISC_ERR_STATUS_MISC_FW_AUTH_FAILED_ERR_SMASK |
		  MISC_ERR_STATUS_MISC_KEY_MISMATCH_ERR_SMASK);
	/*
	 * All that is left are the current errors.  Print warnings on
	 * authorization failure details, if any.  Firmware authorization
	 * can be retried, so these are only warnings.
	 */
	reg = read_csr(dd, MISC_ERR_STATUS);
	if (ret) {
		if (reg & MISC_ERR_STATUS_MISC_FW_AUTH_FAILED_ERR_SMASK)
			dd_dev_warn(dd, "%s firmware authorization failed\n",
				    who);
		if (reg & MISC_ERR_STATUS_MISC_KEY_MISMATCH_ERR_SMASK)
			dd_dev_warn(dd, "%s firmware key mismatch\n", who);
	}

	return ret;
}

static void load_security_variables(struct hfi1_devdata *dd,
				    struct firmware_details *fdet)
{
	/* Security variables a.  Write the modulus */
	write_rsa_data(dd, MISC_CFG_RSA_MODULUS, fdet->modulus, KEY_SIZE);
	/* Security variables b.  Write the r2 */
	write_rsa_data(dd, MISC_CFG_RSA_R2, fdet->r2, KEY_SIZE);
	/* Security variables c.  Write the mu */
	write_rsa_data(dd, MISC_CFG_RSA_MU, fdet->mu, MU_SIZE);
	/* Security variables d.  Write the header */
	write_streamed_rsa_data(dd, MISC_CFG_SHA_PRELOAD,
				(u8 *)fdet->css_header,
				sizeof(struct css_header));
}

/* return the 8051 firmware state */
static inline u32 get_firmware_state(struct hfi1_devdata *dd)
{
	u64 reg = read_csr(dd, DC_DC8051_STS_CUR_STATE);

	return (reg >> DC_DC8051_STS_CUR_STATE_FIRMWARE_SHIFT)
				& DC_DC8051_STS_CUR_STATE_FIRMWARE_MASK;
}

/*
 * Wait until the firmware is up and ready to take host requests.
 * Return 0 on success, -ETIMEDOUT on timeout.
 */
int wait_fm_ready(struct hfi1_devdata *dd, u32 mstimeout)
{
	unsigned long timeout;

	/* in the simulator, the fake 8051 is always ready */
	if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR)
		return 0;

	timeout = msecs_to_jiffies(mstimeout) + jiffies;
	while (1) {
		if (get_firmware_state(dd) == 0xa0)	/* ready */
			return 0;
		if (time_after(jiffies, timeout))	/* timed out */
			return -ETIMEDOUT;
		usleep_range(1950, 2050); /* sleep 2ms-ish */
	}
}

/*
 * Load the 8051 firmware.
 */
static int load_8051_firmware(struct hfi1_devdata *dd,
			      struct firmware_details *fdet)
{
	u64 reg;
	int ret;
	u8 ver_major;
	u8 ver_minor;
	u8 ver_patch;

	/*
	 * DC Reset sequence
	 * Load DC 8051 firmware
	 */
	/*
	 * DC reset step 1: Reset DC8051
	 */
	reg = DC_DC8051_CFG_RST_M8051W_SMASK
		| DC_DC8051_CFG_RST_CRAM_SMASK
		| DC_DC8051_CFG_RST_DRAM_SMASK
		| DC_DC8051_CFG_RST_IRAM_SMASK
		| DC_DC8051_CFG_RST_SFR_SMASK;
	write_csr(dd, DC_DC8051_CFG_RST, reg);

	/*
	 * DC reset step 2 (optional): Load 8051 data memory with link
	 * configuration
	 */

	/*
	 * DC reset step 3: Load DC8051 firmware
	 */
	/* release all but the core reset */
	reg = DC_DC8051_CFG_RST_M8051W_SMASK;
	write_csr(dd, DC_DC8051_CFG_RST, reg);

	/* Firmware load step 1 */
	load_security_variables(dd, fdet);

	/*
	 * Firmware load step 2.  Clear MISC_CFG_FW_CTRL.FW_8051_LOADED
	 */
	write_csr(dd, MISC_CFG_FW_CTRL, 0);

	/* Firmware load steps 3-5 */
	ret = write_8051(dd, 1/*code*/, 0, fdet->firmware_ptr,
			 fdet->firmware_len);
	if (ret)
		return ret;

	/*
	 * DC reset step 4. Host starts the DC8051 firmware
	 */
	/*
	 * Firmware load step 6.  Set MISC_CFG_FW_CTRL.FW_8051_LOADED
	 */
	write_csr(dd, MISC_CFG_FW_CTRL, MISC_CFG_FW_CTRL_FW_8051_LOADED_SMASK);

	/* Firmware load steps 7-10 */
	ret = run_rsa(dd, "8051", fdet->signature);
	if (ret)
		return ret;

	/* clear all reset bits, releasing the 8051 */
	write_csr(dd, DC_DC8051_CFG_RST, 0ull);

	/*
	 * DC reset step 5. Wait for firmware to be ready to accept host
	 * requests.
	 */
	ret = wait_fm_ready(dd, TIMEOUT_8051_START);
	if (ret) { /* timed out */
		dd_dev_err(dd, "8051 start timeout, current state 0x%x\n",
			   get_firmware_state(dd));
		return -ETIMEDOUT;
	}

	read_misc_status(dd, &ver_major, &ver_minor, &ver_patch);
	dd_dev_info(dd, "8051 firmware version %d.%d.%d\n",
		    (int)ver_major, (int)ver_minor, (int)ver_patch);
	dd->dc8051_ver = dc8051_ver(ver_major, ver_minor, ver_patch);
	ret = write_host_interface_version(dd, HOST_INTERFACE_VERSION);
	if (ret != HCMD_SUCCESS) {
		dd_dev_err(dd,
			   "Failed to set host interface version, return 0x%x\n",
			   ret);
		return -EIO;
	}

	return 0;
}

/*
 * Write the SBus request register
 *
 * No need for masking - the arguments are sized exactly.
 */
void sbus_request(struct hfi1_devdata *dd,
		  u8 receiver_addr, u8 data_addr, u8 command, u32 data_in)
{
	write_csr(dd, ASIC_CFG_SBUS_REQUEST,
		  ((u64)data_in << ASIC_CFG_SBUS_REQUEST_DATA_IN_SHIFT) |
		  ((u64)command << ASIC_CFG_SBUS_REQUEST_COMMAND_SHIFT) |
		  ((u64)data_addr << ASIC_CFG_SBUS_REQUEST_DATA_ADDR_SHIFT) |
		  ((u64)receiver_addr <<
		   ASIC_CFG_SBUS_REQUEST_RECEIVER_ADDR_SHIFT));
}

/*
 * Read a value from the SBus.
 *
 * Requires the caller to be in fast mode
 */
static u32 sbus_read(struct hfi1_devdata *dd, u8 receiver_addr, u8 data_addr,
		     u32 data_in)
{
	u64 reg;
	int retries;
	int success = 0;
	u32 result = 0;
	u32 result_code = 0;

	sbus_request(dd, receiver_addr, data_addr, READ_SBUS_RECEIVER, data_in);

	for (retries = 0; retries < 100; retries++) {
		usleep_range(1000, 1200); /* arbitrary */
		reg = read_csr(dd, ASIC_STS_SBUS_RESULT);
		result_code = (reg >> ASIC_STS_SBUS_RESULT_RESULT_CODE_SHIFT)
				& ASIC_STS_SBUS_RESULT_RESULT_CODE_MASK;
		if (result_code != SBUS_READ_COMPLETE)
			continue;

		success = 1;
		result = (reg >> ASIC_STS_SBUS_RESULT_DATA_OUT_SHIFT)
			   & ASIC_STS_SBUS_RESULT_DATA_OUT_MASK;
		break;
	}

	if (!success) {
		dd_dev_err(dd, "%s: read failed, result code 0x%x\n", __func__,
			   result_code);
	}

	return result;
}

/*
 * Turn off the SBus and fabric serdes spicos.
 *
 * + Must be called with Sbus fast mode turned on.
 * + Must be called after fabric serdes broadcast is set up.
 * + Must be called before the 8051 is loaded - assumes 8051 is not loaded
 *   when using MISC_CFG_FW_CTRL.
 */
static void turn_off_spicos(struct hfi1_devdata *dd, int flags)
{
	/* only needed on A0 */
	if (!is_ax(dd))
		return;

	dd_dev_info(dd, "Turning off spicos:%s%s\n",
		    flags & SPICO_SBUS ? " SBus" : "",
		    flags & SPICO_FABRIC ? " fabric" : "");

	write_csr(dd, MISC_CFG_FW_CTRL, ENABLE_SPICO_SMASK);
	/* disable SBus spico */
	if (flags & SPICO_SBUS)
		sbus_request(dd, SBUS_MASTER_BROADCAST, 0x01,
			     WRITE_SBUS_RECEIVER, 0x00000040);

	/* disable the fabric serdes spicos */
	if (flags & SPICO_FABRIC)
		sbus_request(dd, fabric_serdes_broadcast[dd->hfi1_id],
			     0x07, WRITE_SBUS_RECEIVER, 0x00000000);
	write_csr(dd, MISC_CFG_FW_CTRL, 0);
}

/*
 * Reset all of the fabric serdes for this HFI in preparation to take the
 * link to Polling.
 *
 * To do a reset, we need to write to to the serdes registers.  Unfortunately,
 * the fabric serdes download to the other HFI on the ASIC will have turned
 * off the firmware validation on this HFI.  This means we can't write to the
 * registers to reset the serdes.  Work around this by performing a complete
 * re-download and validation of the fabric serdes firmware.  This, as a
 * by-product, will reset the serdes.  NOTE: the re-download requires that
 * the 8051 be in the Offline state.  I.e. not actively trying to use the
 * serdes.  This routine is called at the point where the link is Offline and
 * is getting ready to go to Polling.
 */
void fabric_serdes_reset(struct hfi1_devdata *dd)
{
	int ret;

	if (!fw_fabric_serdes_load)
		return;

	ret = acquire_chip_resource(dd, CR_SBUS, SBUS_TIMEOUT);
	if (ret) {
		dd_dev_err(dd,
			   "Cannot acquire SBus resource to reset fabric SerDes - perhaps you should reboot\n");
		return;
	}
	set_sbus_fast_mode(dd);

	if (is_ax(dd)) {
		/* A0 serdes do not work with a re-download */
		u8 ra = fabric_serdes_broadcast[dd->hfi1_id];

		/* place SerDes in reset and disable SPICO */
		sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000011);
		/* wait 100 refclk cycles @ 156.25MHz => 640ns */
		udelay(1);
		/* remove SerDes reset */
		sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000010);
		/* turn SPICO enable on */
		sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000002);
	} else {
		turn_off_spicos(dd, SPICO_FABRIC);
		/*
		 * No need for firmware retry - what to download has already
		 * been decided.
		 * No need to pay attention to the load return - the only
		 * failure is a validation failure, which has already been
		 * checked by the initial download.
		 */
		(void)load_fabric_serdes_firmware(dd, &fw_fabric);
	}

	clear_sbus_fast_mode(dd);
	release_chip_resource(dd, CR_SBUS);
}

/* Access to the SBus in this routine should probably be serialized */
int sbus_request_slow(struct hfi1_devdata *dd,
		      u8 receiver_addr, u8 data_addr, u8 command, u32 data_in)
{
	u64 reg, count = 0;

	/* make sure fast mode is clear */
	clear_sbus_fast_mode(dd);

	sbus_request(dd, receiver_addr, data_addr, command, data_in);
	write_csr(dd, ASIC_CFG_SBUS_EXECUTE,
		  ASIC_CFG_SBUS_EXECUTE_EXECUTE_SMASK);
	/* Wait for both DONE and RCV_DATA_VALID to go high */
	reg = read_csr(dd, ASIC_STS_SBUS_RESULT);
	while (!((reg & ASIC_STS_SBUS_RESULT_DONE_SMASK) &&
		 (reg & ASIC_STS_SBUS_RESULT_RCV_DATA_VALID_SMASK))) {
		if (count++ >= SBUS_MAX_POLL_COUNT) {
			u64 counts = read_csr(dd, ASIC_STS_SBUS_COUNTERS);
			/*
			 * If the loop has timed out, we are OK if DONE bit
			 * is set and RCV_DATA_VALID and EXECUTE counters
			 * are the same. If not, we cannot proceed.
			 */
			if ((reg & ASIC_STS_SBUS_RESULT_DONE_SMASK) &&
			    (SBUS_COUNTER(counts, RCV_DATA_VALID) ==
			     SBUS_COUNTER(counts, EXECUTE)))
				break;
			return -ETIMEDOUT;
		}
		udelay(1);
		reg = read_csr(dd, ASIC_STS_SBUS_RESULT);
	}
	count = 0;
	write_csr(dd, ASIC_CFG_SBUS_EXECUTE, 0);
	/* Wait for DONE to clear after EXECUTE is cleared */
	reg = read_csr(dd, ASIC_STS_SBUS_RESULT);
	while (reg & ASIC_STS_SBUS_RESULT_DONE_SMASK) {
		if (count++ >= SBUS_MAX_POLL_COUNT)
			return -ETIME;
		udelay(1);
		reg = read_csr(dd, ASIC_STS_SBUS_RESULT);
	}
	return 0;
}

static int load_fabric_serdes_firmware(struct hfi1_devdata *dd,
				       struct firmware_details *fdet)
{
	int i, err;
	const u8 ra = fabric_serdes_broadcast[dd->hfi1_id]; /* receiver addr */

	dd_dev_info(dd, "Downloading fabric firmware\n");

	/* step 1: load security variables */
	load_security_variables(dd, fdet);
	/* step 2: place SerDes in reset and disable SPICO */
	sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000011);
	/* wait 100 refclk cycles @ 156.25MHz => 640ns */
	udelay(1);
	/* step 3:  remove SerDes reset */
	sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000010);
	/* step 4: assert IMEM override */
	sbus_request(dd, ra, 0x00, WRITE_SBUS_RECEIVER, 0x40000000);
	/* step 5: download SerDes machine code */
	for (i = 0; i < fdet->firmware_len; i += 4) {
		sbus_request(dd, ra, 0x0a, WRITE_SBUS_RECEIVER,
			     *(u32 *)&fdet->firmware_ptr[i]);
	}
	/* step 6: IMEM override off */
	sbus_request(dd, ra, 0x00, WRITE_SBUS_RECEIVER, 0x00000000);
	/* step 7: turn ECC on */
	sbus_request(dd, ra, 0x0b, WRITE_SBUS_RECEIVER, 0x000c0000);

	/* steps 8-11: run the RSA engine */
	err = run_rsa(dd, "fabric serdes", fdet->signature);
	if (err)
		return err;

	/* step 12: turn SPICO enable on */
	sbus_request(dd, ra, 0x07, WRITE_SBUS_RECEIVER, 0x00000002);
	/* step 13: enable core hardware interrupts */
	sbus_request(dd, ra, 0x08, WRITE_SBUS_RECEIVER, 0x00000000);

	return 0;
}

static int load_sbus_firmware(struct hfi1_devdata *dd,
			      struct firmware_details *fdet)
{
	int i, err;
	const u8 ra = SBUS_MASTER_BROADCAST; /* receiver address */

	dd_dev_info(dd, "Downloading SBus firmware\n");

	/* step 1: load security variables */
	load_security_variables(dd, fdet);
	/* step 2: place SPICO into reset and enable off */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x000000c0);
	/* step 3: remove reset, enable off, IMEM_CNTRL_EN on */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x00000240);
	/* step 4: set starting IMEM address for burst download */
	sbus_request(dd, ra, 0x03, WRITE_SBUS_RECEIVER, 0x80000000);
	/* step 5: download the SBus Master machine code */
	for (i = 0; i < fdet->firmware_len; i += 4) {
		sbus_request(dd, ra, 0x14, WRITE_SBUS_RECEIVER,
			     *(u32 *)&fdet->firmware_ptr[i]);
	}
	/* step 6: set IMEM_CNTL_EN off */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x00000040);
	/* step 7: turn ECC on */
	sbus_request(dd, ra, 0x16, WRITE_SBUS_RECEIVER, 0x000c0000);

	/* steps 8-11: run the RSA engine */
	err = run_rsa(dd, "SBus", fdet->signature);
	if (err)
		return err;

	/* step 12: set SPICO_ENABLE on */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x00000140);

	return 0;
}

static int load_pcie_serdes_firmware(struct hfi1_devdata *dd,
				     struct firmware_details *fdet)
{
	int i;
	const u8 ra = SBUS_MASTER_BROADCAST; /* receiver address */

	dd_dev_info(dd, "Downloading PCIe firmware\n");

	/* step 1: load security variables */
	load_security_variables(dd, fdet);
	/* step 2: assert single step (halts the SBus Master spico) */
	sbus_request(dd, ra, 0x05, WRITE_SBUS_RECEIVER, 0x00000001);
	/* step 3: enable XDMEM access */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x00000d40);
	/* step 4: load firmware into SBus Master XDMEM */
	/*
	 * NOTE: the dmem address, write_en, and wdata are all pre-packed,
	 * we only need to pick up the bytes and write them
	 */
	for (i = 0; i < fdet->firmware_len; i += 4) {
		sbus_request(dd, ra, 0x04, WRITE_SBUS_RECEIVER,
			     *(u32 *)&fdet->firmware_ptr[i]);
	}
	/* step 5: disable XDMEM access */
	sbus_request(dd, ra, 0x01, WRITE_SBUS_RECEIVER, 0x00000140);
	/* step 6: allow SBus Spico to run */
	sbus_request(dd, ra, 0x05, WRITE_SBUS_RECEIVER, 0x00000000);

	/*
	 * steps 7-11: run RSA, if it succeeds, firmware is available to
	 * be swapped
	 */
	return run_rsa(dd, "PCIe serdes", fdet->signature);
}

/*
 * Set the given broadcast values on the given list of devices.
 */
static void set_serdes_broadcast(struct hfi1_devdata *dd, u8 bg1, u8 bg2,
				 const u8 *addrs, int count)
{
	while (--count >= 0) {
		/*
		 * Set BROADCAST_GROUP_1 and BROADCAST_GROUP_2, leave
		 * defaults for everything else.  Do not read-modify-write,
		 * per instruction from the manufacturer.
		 *
		 * Register 0xfd:
		 *	bits    what
		 *	-----	---------------------------------
		 *	  0	IGNORE_BROADCAST  (default 0)
		 *	11:4	BROADCAST_GROUP_1 (default 0xff)
		 *	23:16	BROADCAST_GROUP_2 (default 0xff)
		 */
		sbus_request(dd, addrs[count], 0xfd, WRITE_SBUS_RECEIVER,
			     (u32)bg1 << 4 | (u32)bg2 << 16);
	}
}

int acquire_hw_mutex(struct hfi1_devdata *dd)
{
	unsigned long timeout;
	int try = 0;
	u8 mask = 1 << dd->hfi1_id;
	u8 user = (u8)read_csr(dd, ASIC_CFG_MUTEX);

	if (user == mask) {
		dd_dev_info(dd,
			    "Hardware mutex already acquired, mutex mask %u\n",
			    (u32)mask);
		return 0;
	}

retry:
	timeout = msecs_to_jiffies(HM_TIMEOUT) + jiffies;
	while (1) {
		write_csr(dd, ASIC_CFG_MUTEX, mask);
		user = (u8)read_csr(dd, ASIC_CFG_MUTEX);
		if (user == mask)
			return 0; /* success */
		if (time_after(jiffies, timeout))
			break; /* timed out */
		msleep(20);
	}

	/* timed out */
	dd_dev_err(dd,
		   "Unable to acquire hardware mutex, mutex mask %u, my mask %u (%s)\n",
		   (u32)user, (u32)mask, (try == 0) ? "retrying" : "giving up");

	if (try == 0) {
		/* break mutex and retry */
		write_csr(dd, ASIC_CFG_MUTEX, 0);
		try++;
		goto retry;
	}

	return -EBUSY;
}

void release_hw_mutex(struct hfi1_devdata *dd)
{
	u8 mask = 1 << dd->hfi1_id;
	u8 user = (u8)read_csr(dd, ASIC_CFG_MUTEX);

	if (user != mask)
		dd_dev_warn(dd,
			    "Unable to release hardware mutex, mutex mask %u, my mask %u\n",
			    (u32)user, (u32)mask);
	else
		write_csr(dd, ASIC_CFG_MUTEX, 0);
}

/* return the given resource bit(s) as a mask for the given HFI */
static inline u64 resource_mask(u32 hfi1_id, u32 resource)
{
	return ((u64)resource) << (hfi1_id ? CR_DYN_SHIFT : 0);
}

static void fail_mutex_acquire_message(struct hfi1_devdata *dd,
				       const char *func)
{
	dd_dev_err(dd,
		   "%s: hardware mutex stuck - suggest rebooting the machine\n",
		   func);
}

/*
 * Acquire access to a chip resource.
 *
 * Return 0 on success, -EBUSY if resource busy, -EIO if mutex acquire failed.
 */
static int __acquire_chip_resource(struct hfi1_devdata *dd, u32 resource)
{
	u64 scratch0, all_bits, my_bit;
	int ret;

	if (resource & CR_DYN_MASK) {
		/* a dynamic resource is in use if either HFI has set the bit */
		if (dd->pcidev->device == PCI_DEVICE_ID_INTEL0 &&
		    (resource & (CR_I2C1 | CR_I2C2))) {
			/* discrete devices must serialize across both chains */
			all_bits = resource_mask(0, CR_I2C1 | CR_I2C2) |
					resource_mask(1, CR_I2C1 | CR_I2C2);
		} else {
			all_bits = resource_mask(0, resource) |
						resource_mask(1, resource);
		}
		my_bit = resource_mask(dd->hfi1_id, resource);
	} else {
		/* non-dynamic resources are not split between HFIs */
		all_bits = resource;
		my_bit = resource;
	}

	/* lock against other callers within the driver wanting a resource */
	mutex_lock(&dd->asic_data->asic_resource_mutex);

	ret = acquire_hw_mutex(dd);
	if (ret) {
		fail_mutex_acquire_message(dd, __func__);
		ret = -EIO;
		goto done;
	}

	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	if (scratch0 & all_bits) {
		ret = -EBUSY;
	} else {
		write_csr(dd, ASIC_CFG_SCRATCH, scratch0 | my_bit);
		/* force write to be visible to other HFI on another OS */
		(void)read_csr(dd, ASIC_CFG_SCRATCH);
	}

	release_hw_mutex(dd);

done:
	mutex_unlock(&dd->asic_data->asic_resource_mutex);
	return ret;
}

/*
 * Acquire access to a chip resource, wait up to mswait milliseconds for
 * the resource to become available.
 *
 * Return 0 on success, -EBUSY if busy (even after wait), -EIO if mutex
 * acquire failed.
 */
int acquire_chip_resource(struct hfi1_devdata *dd, u32 resource, u32 mswait)
{
	unsigned long timeout;
	int ret;

	timeout = jiffies + msecs_to_jiffies(mswait);
	while (1) {
		ret = __acquire_chip_resource(dd, resource);
		if (ret != -EBUSY)
			return ret;
		/* resource is busy, check our timeout */
		if (time_after_eq(jiffies, timeout))
			return -EBUSY;
		usleep_range(80, 120);	/* arbitrary delay */
	}
}

/*
 * Release access to a chip resource
 */
void release_chip_resource(struct hfi1_devdata *dd, u32 resource)
{
	u64 scratch0, bit;

	/* only dynamic resources should ever be cleared */
	if (!(resource & CR_DYN_MASK)) {
		dd_dev_err(dd, "%s: invalid resource 0x%x\n", __func__,
			   resource);
		return;
	}
	bit = resource_mask(dd->hfi1_id, resource);

	/* lock against other callers within the driver wanting a resource */
	mutex_lock(&dd->asic_data->asic_resource_mutex);

	if (acquire_hw_mutex(dd)) {
		fail_mutex_acquire_message(dd, __func__);
		goto done;
	}

	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	if ((scratch0 & bit) != 0) {
		scratch0 &= ~bit;
		write_csr(dd, ASIC_CFG_SCRATCH, scratch0);
		/* force write to be visible to other HFI on another OS */
		(void)read_csr(dd, ASIC_CFG_SCRATCH);
	} else {
		dd_dev_warn(dd, "%s: id %d, resource 0x%x: bit not set\n",
			    __func__, dd->hfi1_id, resource);
	}

	release_hw_mutex(dd);

done:
	mutex_unlock(&dd->asic_data->asic_resource_mutex);
}

/*
 * Return true if resource is set, false otherwise.  Print a warning
 * if not set and a function is supplied.
 */
bool check_chip_resource(struct hfi1_devdata *dd, u32 resource,
			 const char *func)
{
	u64 scratch0, bit;

	if (resource & CR_DYN_MASK)
		bit = resource_mask(dd->hfi1_id, resource);
	else
		bit = resource;

	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	if ((scratch0 & bit) == 0) {
		if (func)
			dd_dev_warn(dd,
				    "%s: id %d, resource 0x%x, not acquired!\n",
				    func, dd->hfi1_id, resource);
		return false;
	}
	return true;
}

static void clear_chip_resources(struct hfi1_devdata *dd, const char *func)
{
	u64 scratch0;

	/* lock against other callers within the driver wanting a resource */
	mutex_lock(&dd->asic_data->asic_resource_mutex);

	if (acquire_hw_mutex(dd)) {
		fail_mutex_acquire_message(dd, func);
		goto done;
	}

	/* clear all dynamic access bits for this HFI */
	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	scratch0 &= ~resource_mask(dd->hfi1_id, CR_DYN_MASK);
	write_csr(dd, ASIC_CFG_SCRATCH, scratch0);
	/* force write to be visible to other HFI on another OS */
	(void)read_csr(dd, ASIC_CFG_SCRATCH);

	release_hw_mutex(dd);

done:
	mutex_unlock(&dd->asic_data->asic_resource_mutex);
}

void init_chip_resources(struct hfi1_devdata *dd)
{
	/* clear any holds left by us */
	clear_chip_resources(dd, __func__);
}

void finish_chip_resources(struct hfi1_devdata *dd)
{
	/* clear any holds left by us */
	clear_chip_resources(dd, __func__);
}

void set_sbus_fast_mode(struct hfi1_devdata *dd)
{
	write_csr(dd, ASIC_CFG_SBUS_EXECUTE,
		  ASIC_CFG_SBUS_EXECUTE_FAST_MODE_SMASK);
}

void clear_sbus_fast_mode(struct hfi1_devdata *dd)
{
	u64 reg, count = 0;

	reg = read_csr(dd, ASIC_STS_SBUS_COUNTERS);
	while (SBUS_COUNTER(reg, EXECUTE) !=
	       SBUS_COUNTER(reg, RCV_DATA_VALID)) {
		if (count++ >= SBUS_MAX_POLL_COUNT)
			break;
		udelay(1);
		reg = read_csr(dd, ASIC_STS_SBUS_COUNTERS);
	}
	write_csr(dd, ASIC_CFG_SBUS_EXECUTE, 0);
}

int load_firmware(struct hfi1_devdata *dd)
{
	int ret;

	if (fw_fabric_serdes_load) {
		ret = acquire_chip_resource(dd, CR_SBUS, SBUS_TIMEOUT);
		if (ret)
			return ret;

		set_sbus_fast_mode(dd);

		set_serdes_broadcast(dd, all_fabric_serdes_broadcast,
				     fabric_serdes_broadcast[dd->hfi1_id],
				     fabric_serdes_addrs[dd->hfi1_id],
				     NUM_FABRIC_SERDES);
		turn_off_spicos(dd, SPICO_FABRIC);
		do {
			ret = load_fabric_serdes_firmware(dd, &fw_fabric);
		} while (retry_firmware(dd, ret));

		clear_sbus_fast_mode(dd);
		release_chip_resource(dd, CR_SBUS);
		if (ret)
			return ret;
	}

	if (fw_8051_load) {
		do {
			ret = load_8051_firmware(dd, &fw_8051);
		} while (retry_firmware(dd, ret));
		if (ret)
			return ret;
	}

	dump_fw_version(dd);
	return 0;
}

int hfi1_firmware_init(struct hfi1_devdata *dd)
{
	/* only RTL can use these */
	if (dd->icode != ICODE_RTL_SILICON) {
		fw_fabric_serdes_load = 0;
		fw_pcie_serdes_load = 0;
		fw_sbus_load = 0;
	}

	/* no 8051 or QSFP on simulator */
	if (dd->icode == ICODE_FUNCTIONAL_SIMULATOR)
		fw_8051_load = 0;

	if (!fw_8051_name) {
		if (dd->icode == ICODE_RTL_SILICON)
			fw_8051_name = DEFAULT_FW_8051_NAME_ASIC;
		else
			fw_8051_name = DEFAULT_FW_8051_NAME_FPGA;
	}
	if (!fw_fabric_serdes_name)
		fw_fabric_serdes_name = DEFAULT_FW_FABRIC_NAME;
	if (!fw_sbus_name)
		fw_sbus_name = DEFAULT_FW_SBUS_NAME;
	if (!fw_pcie_serdes_name)
		fw_pcie_serdes_name = DEFAULT_FW_PCIE_NAME;

	return obtain_firmware(dd);
}

/*
 * This function is a helper function for parse_platform_config(...) and
 * does not check for validity of the platform configuration cache
 * (because we know it is invalid as we are building up the cache).
 * As such, this should not be called from anywhere other than
 * parse_platform_config
 */
static int check_meta_version(struct hfi1_devdata *dd, u32 *system_table)
{
	u32 meta_ver, meta_ver_meta, ver_start, ver_len, mask;
	struct platform_config_cache *pcfgcache = &dd->pcfg_cache;

	if (!system_table)
		return -EINVAL;

	meta_ver_meta =
	*(pcfgcache->config_tables[PLATFORM_CONFIG_SYSTEM_TABLE].table_metadata
	+ SYSTEM_TABLE_META_VERSION);

	mask = ((1 << METADATA_TABLE_FIELD_START_LEN_BITS) - 1);
	ver_start = meta_ver_meta & mask;

	meta_ver_meta >>= METADATA_TABLE_FIELD_LEN_SHIFT;

	mask = ((1 << METADATA_TABLE_FIELD_LEN_LEN_BITS) - 1);
	ver_len = meta_ver_meta & mask;

	ver_start /= 8;
	meta_ver = *((u8 *)system_table + ver_start) & ((1 << ver_len) - 1);

	if (meta_ver < 4) {
		dd_dev_info(
			dd, "%s:Please update platform config\n", __func__);
		return -EINVAL;
	}
	return 0;
}

int parse_platform_config(struct hfi1_devdata *dd)
{
	struct platform_config_cache *pcfgcache = &dd->pcfg_cache;
	struct hfi1_pportdata *ppd = dd->pport;
	u32 *ptr = NULL;
	u32 header1 = 0, header2 = 0, magic_num = 0, crc = 0, file_length = 0;
	u32 record_idx = 0, table_type = 0, table_length_dwords = 0;
	int ret = -EINVAL; /* assume failure */

	/*
	 * For integrated devices that did not fall back to the default file,
	 * the SI tuning information for active channels is acquired from the
	 * scratch register bitmap, thus there is no platform config to parse.
	 * Skip parsing in these situations.
	 */
	if (ppd->config_from_scratch)
		return 0;

	if (!dd->platform_config.data) {
		dd_dev_err(dd, "%s: Missing config file\n", __func__);
		goto bail;
	}
	ptr = (u32 *)dd->platform_config.data;

	magic_num = *ptr;
	ptr++;
	if (magic_num != PLATFORM_CONFIG_MAGIC_NUM) {
		dd_dev_err(dd, "%s: Bad config file\n", __func__);
		goto bail;
	}

	/* Field is file size in DWORDs */
	file_length = (*ptr) * 4;

	/*
	 * Length can't be larger than partition size. Assume platform
	 * config format version 4 is being used. Interpret the file size
	 * field as header instead by not moving the pointer.
	 */
	if (file_length > MAX_PLATFORM_CONFIG_FILE_SIZE) {
		dd_dev_info(dd,
			    "%s:File length out of bounds, using alternative format\n",
			    __func__);
		file_length = PLATFORM_CONFIG_FORMAT_4_FILE_SIZE;
	} else {
		ptr++;
	}

	if (file_length > dd->platform_config.size) {
		dd_dev_info(dd, "%s:File claims to be larger than read size\n",
			    __func__);
		goto bail;
	} else if (file_length < dd->platform_config.size) {
		dd_dev_info(dd,
			    "%s:File claims to be smaller than read size, continuing\n",
			    __func__);
	}
	/* exactly equal, perfection */

	/*
	 * In both cases where we proceed, using the self-reported file length
	 * is the safer option. In case of old format a predefined value is
	 * being used.
	 */
	while (ptr < (u32 *)(dd->platform_config.data + file_length)) {
		header1 = *ptr;
		header2 = *(ptr + 1);
		if (header1 != ~header2) {
			dd_dev_err(dd, "%s: Failed validation at offset %ld\n",
				   __func__, (ptr - (u32 *)
					      dd->platform_config.data));
			goto bail;
		}

		record_idx = *ptr &
			((1 << PLATFORM_CONFIG_HEADER_RECORD_IDX_LEN_BITS) - 1);

		table_length_dwords = (*ptr >>
				PLATFORM_CONFIG_HEADER_TABLE_LENGTH_SHIFT) &
		      ((1 << PLATFORM_CONFIG_HEADER_TABLE_LENGTH_LEN_BITS) - 1);

		table_type = (*ptr >> PLATFORM_CONFIG_HEADER_TABLE_TYPE_SHIFT) &
			((1 << PLATFORM_CONFIG_HEADER_TABLE_TYPE_LEN_BITS) - 1);

		/* Done with this set of headers */
		ptr += 2;

		if (record_idx) {
			/* data table */
			switch (table_type) {
			case PLATFORM_CONFIG_SYSTEM_TABLE:
				pcfgcache->config_tables[table_type].num_table =
									1;
				ret = check_meta_version(dd, ptr);
				if (ret)
					goto bail;
				break;
			case PLATFORM_CONFIG_PORT_TABLE:
				pcfgcache->config_tables[table_type].num_table =
									2;
				break;
			case PLATFORM_CONFIG_RX_PRESET_TABLE:
			case PLATFORM_CONFIG_TX_PRESET_TABLE:
			case PLATFORM_CONFIG_QSFP_ATTEN_TABLE:
			case PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE:
				pcfgcache->config_tables[table_type].num_table =
							table_length_dwords;
				break;
			default:
				dd_dev_err(dd,
					   "%s: Unknown data table %d, offset %ld\n",
					   __func__, table_type,
					   (ptr - (u32 *)
					    dd->platform_config.data));
				goto bail; /* We don't trust this file now */
			}
			pcfgcache->config_tables[table_type].table = ptr;
		} else {
			/* metadata table */
			switch (table_type) {
			case PLATFORM_CONFIG_SYSTEM_TABLE:
			case PLATFORM_CONFIG_PORT_TABLE:
			case PLATFORM_CONFIG_RX_PRESET_TABLE:
			case PLATFORM_CONFIG_TX_PRESET_TABLE:
			case PLATFORM_CONFIG_QSFP_ATTEN_TABLE:
			case PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE:
				break;
			default:
				dd_dev_err(dd,
					   "%s: Unknown meta table %d, offset %ld\n",
					   __func__, table_type,
					   (ptr -
					    (u32 *)dd->platform_config.data));
				goto bail; /* We don't trust this file now */
			}
			pcfgcache->config_tables[table_type].table_metadata =
									ptr;
		}

		/* Calculate and check table crc */
		crc = crc32_le(~(u32)0, (unsigned char const *)ptr,
			       (table_length_dwords * 4));
		crc ^= ~(u32)0;

		/* Jump the table */
		ptr += table_length_dwords;
		if (crc != *ptr) {
			dd_dev_err(dd, "%s: Failed CRC check at offset %ld\n",
				   __func__, (ptr -
				   (u32 *)dd->platform_config.data));
			ret = -EINVAL;
			goto bail;
		}
		/* Jump the CRC DWORD */
		ptr++;
	}

	pcfgcache->cache_valid = 1;
	return 0;
bail:
	memset(pcfgcache, 0, sizeof(struct platform_config_cache));
	return ret;
}

static void get_integrated_platform_config_field(
		struct hfi1_devdata *dd,
		enum platform_config_table_type_encoding table_type,
		int field_index, u32 *data)
{
	struct hfi1_pportdata *ppd = dd->pport;
	u8 *cache = ppd->qsfp_info.cache;
	u32 tx_preset = 0;

	switch (table_type) {
	case PLATFORM_CONFIG_SYSTEM_TABLE:
		if (field_index == SYSTEM_TABLE_QSFP_POWER_CLASS_MAX)
			*data = ppd->max_power_class;
		else if (field_index == SYSTEM_TABLE_QSFP_ATTENUATION_DEFAULT_25G)
			*data = ppd->default_atten;
		break;
	case PLATFORM_CONFIG_PORT_TABLE:
		if (field_index == PORT_TABLE_PORT_TYPE)
			*data = ppd->port_type;
		else if (field_index == PORT_TABLE_LOCAL_ATTEN_25G)
			*data = ppd->local_atten;
		else if (field_index == PORT_TABLE_REMOTE_ATTEN_25G)
			*data = ppd->remote_atten;
		break;
	case PLATFORM_CONFIG_RX_PRESET_TABLE:
		if (field_index == RX_PRESET_TABLE_QSFP_RX_CDR_APPLY)
			*data = (ppd->rx_preset & QSFP_RX_CDR_APPLY_SMASK) >>
				QSFP_RX_CDR_APPLY_SHIFT;
		else if (field_index == RX_PRESET_TABLE_QSFP_RX_EMP_APPLY)
			*data = (ppd->rx_preset & QSFP_RX_EMP_APPLY_SMASK) >>
				QSFP_RX_EMP_APPLY_SHIFT;
		else if (field_index == RX_PRESET_TABLE_QSFP_RX_AMP_APPLY)
			*data = (ppd->rx_preset & QSFP_RX_AMP_APPLY_SMASK) >>
				QSFP_RX_AMP_APPLY_SHIFT;
		else if (field_index == RX_PRESET_TABLE_QSFP_RX_CDR)
			*data = (ppd->rx_preset & QSFP_RX_CDR_SMASK) >>
				QSFP_RX_CDR_SHIFT;
		else if (field_index == RX_PRESET_TABLE_QSFP_RX_EMP)
			*data = (ppd->rx_preset & QSFP_RX_EMP_SMASK) >>
				QSFP_RX_EMP_SHIFT;
		else if (field_index == RX_PRESET_TABLE_QSFP_RX_AMP)
			*data = (ppd->rx_preset & QSFP_RX_AMP_SMASK) >>
				QSFP_RX_AMP_SHIFT;
		break;
	case PLATFORM_CONFIG_TX_PRESET_TABLE:
		if (cache[QSFP_EQ_INFO_OFFS] & 0x4)
			tx_preset = ppd->tx_preset_eq;
		else
			tx_preset = ppd->tx_preset_noeq;
		if (field_index == TX_PRESET_TABLE_PRECUR)
			*data = (tx_preset & TX_PRECUR_SMASK) >>
				TX_PRECUR_SHIFT;
		else if (field_index == TX_PRESET_TABLE_ATTN)
			*data = (tx_preset & TX_ATTN_SMASK) >>
				TX_ATTN_SHIFT;
		else if (field_index == TX_PRESET_TABLE_POSTCUR)
			*data = (tx_preset & TX_POSTCUR_SMASK) >>
				TX_POSTCUR_SHIFT;
		else if (field_index == TX_PRESET_TABLE_QSFP_TX_CDR_APPLY)
			*data = (tx_preset & QSFP_TX_CDR_APPLY_SMASK) >>
				QSFP_TX_CDR_APPLY_SHIFT;
		else if (field_index == TX_PRESET_TABLE_QSFP_TX_EQ_APPLY)
			*data = (tx_preset & QSFP_TX_EQ_APPLY_SMASK) >>
				QSFP_TX_EQ_APPLY_SHIFT;
		else if (field_index == TX_PRESET_TABLE_QSFP_TX_CDR)
			*data = (tx_preset & QSFP_TX_CDR_SMASK) >>
				QSFP_TX_CDR_SHIFT;
		else if (field_index == TX_PRESET_TABLE_QSFP_TX_EQ)
			*data = (tx_preset & QSFP_TX_EQ_SMASK) >>
				QSFP_TX_EQ_SHIFT;
		break;
	case PLATFORM_CONFIG_QSFP_ATTEN_TABLE:
	case PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE:
	default:
		break;
	}
}

static int get_platform_fw_field_metadata(struct hfi1_devdata *dd, int table,
					  int field, u32 *field_len_bits,
					  u32 *field_start_bits)
{
	struct platform_config_cache *pcfgcache = &dd->pcfg_cache;
	u32 *src_ptr = NULL;

	if (!pcfgcache->cache_valid)
		return -EINVAL;

	switch (table) {
	case PLATFORM_CONFIG_SYSTEM_TABLE:
	case PLATFORM_CONFIG_PORT_TABLE:
	case PLATFORM_CONFIG_RX_PRESET_TABLE:
	case PLATFORM_CONFIG_TX_PRESET_TABLE:
	case PLATFORM_CONFIG_QSFP_ATTEN_TABLE:
	case PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE:
		if (field && field < platform_config_table_limits[table])
			src_ptr =
			pcfgcache->config_tables[table].table_metadata + field;
		break;
	default:
		dd_dev_info(dd, "%s: Unknown table\n", __func__);
		break;
	}

	if (!src_ptr)
		return -EINVAL;

	if (field_start_bits)
		*field_start_bits = *src_ptr &
		      ((1 << METADATA_TABLE_FIELD_START_LEN_BITS) - 1);

	if (field_len_bits)
		*field_len_bits = (*src_ptr >> METADATA_TABLE_FIELD_LEN_SHIFT)
		       & ((1 << METADATA_TABLE_FIELD_LEN_LEN_BITS) - 1);

	return 0;
}

/* This is the central interface to getting data out of the platform config
 * file. It depends on parse_platform_config() having populated the
 * platform_config_cache in hfi1_devdata, and checks the cache_valid member to
 * validate the sanity of the cache.
 *
 * The non-obvious parameters:
 * @table_index: Acts as a look up key into which instance of the tables the
 * relevant field is fetched from.
 *
 * This applies to the data tables that have multiple instances. The port table
 * is an exception to this rule as each HFI only has one port and thus the
 * relevant table can be distinguished by hfi_id.
 *
 * @data: pointer to memory that will be populated with the field requested.
 * @len: length of memory pointed by @data in bytes.
 */
int get_platform_config_field(struct hfi1_devdata *dd,
			      enum platform_config_table_type_encoding
			      table_type, int table_index, int field_index,
			      u32 *data, u32 len)
{
	int ret = 0, wlen = 0, seek = 0;
	u32 field_len_bits = 0, field_start_bits = 0, *src_ptr = NULL;
	struct platform_config_cache *pcfgcache = &dd->pcfg_cache;
	struct hfi1_pportdata *ppd = dd->pport;

	if (data)
		memset(data, 0, len);
	else
		return -EINVAL;

	if (ppd->config_from_scratch) {
		/*
		 * Use saved configuration from ppd for integrated platforms
		 */
		get_integrated_platform_config_field(dd, table_type,
						     field_index, data);
		return 0;
	}

	ret = get_platform_fw_field_metadata(dd, table_type, field_index,
					     &field_len_bits,
					     &field_start_bits);
	if (ret)
		return -EINVAL;

	/* Convert length to bits */
	len *= 8;

	/* Our metadata function checked cache_valid and field_index for us */
	switch (table_type) {
	case PLATFORM_CONFIG_SYSTEM_TABLE:
		src_ptr = pcfgcache->config_tables[table_type].table;

		if (field_index != SYSTEM_TABLE_QSFP_POWER_CLASS_MAX) {
			if (len < field_len_bits)
				return -EINVAL;

			seek = field_start_bits / 8;
			wlen = field_len_bits / 8;

			src_ptr = (u32 *)((u8 *)src_ptr + seek);

			/*
			 * We expect the field to be byte aligned and whole byte
			 * lengths if we are here
			 */
			memcpy(data, src_ptr, wlen);
			return 0;
		}
		break;
	case PLATFORM_CONFIG_PORT_TABLE:
		/* Port table is 4 DWORDS */
		src_ptr = dd->hfi1_id ?
			pcfgcache->config_tables[table_type].table + 4 :
			pcfgcache->config_tables[table_type].table;
		break;
	case PLATFORM_CONFIG_RX_PRESET_TABLE:
	case PLATFORM_CONFIG_TX_PRESET_TABLE:
	case PLATFORM_CONFIG_QSFP_ATTEN_TABLE:
	case PLATFORM_CONFIG_VARIABLE_SETTINGS_TABLE:
		src_ptr = pcfgcache->config_tables[table_type].table;

		if (table_index <
			pcfgcache->config_tables[table_type].num_table)
			src_ptr += table_index;
		else
			src_ptr = NULL;
		break;
	default:
		dd_dev_info(dd, "%s: Unknown table\n", __func__);
		break;
	}

	if (!src_ptr || len < field_len_bits)
		return -EINVAL;

	src_ptr += (field_start_bits / 32);
	*data = (*src_ptr >> (field_start_bits % 32)) &
			((1 << field_len_bits) - 1);

	return 0;
}

/*
 * Download the firmware needed for the Gen3 PCIe SerDes.  An update
 * to the SBus firmware is needed before updating the PCIe firmware.
 *
 * Note: caller must be holding the SBus resource.
 */
int load_pcie_firmware(struct hfi1_devdata *dd)
{
	int ret = 0;

	/* both firmware loads below use the SBus */
	set_sbus_fast_mode(dd);

	if (fw_sbus_load) {
		turn_off_spicos(dd, SPICO_SBUS);
		do {
			ret = load_sbus_firmware(dd, &fw_sbus);
		} while (retry_firmware(dd, ret));
		if (ret)
			goto done;
	}

	if (fw_pcie_serdes_load) {
		dd_dev_info(dd, "Setting PCIe SerDes broadcast\n");
		set_serdes_broadcast(dd, all_pcie_serdes_broadcast,
				     pcie_serdes_broadcast[dd->hfi1_id],
				     pcie_serdes_addrs[dd->hfi1_id],
				     NUM_PCIE_SERDES);
		do {
			ret = load_pcie_serdes_firmware(dd, &fw_pcie);
		} while (retry_firmware(dd, ret));
		if (ret)
			goto done;
	}

done:
	clear_sbus_fast_mode(dd);

	return ret;
}

/*
 * Read the GUID from the hardware, store it in dd.
 */
void read_guid(struct hfi1_devdata *dd)
{
	/* Take the DC out of reset to get a valid GUID value */
	write_csr(dd, CCE_DC_CTRL, 0);
	(void)read_csr(dd, CCE_DC_CTRL);

	dd->base_guid = read_csr(dd, DC_DC8051_CFG_LOCAL_GUID);
	dd_dev_info(dd, "GUID %llx",
		    (unsigned long long)dd->base_guid);
}

/* read and display firmware version info */
static void dump_fw_version(struct hfi1_devdata *dd)
{
	u32 pcie_vers[NUM_PCIE_SERDES];
	u32 fabric_vers[NUM_FABRIC_SERDES];
	u32 sbus_vers;
	int i;
	int all_same;
	int ret;
	u8 rcv_addr;

	ret = acquire_chip_resource(dd, CR_SBUS, SBUS_TIMEOUT);
	if (ret) {
		dd_dev_err(dd, "Unable to acquire SBus to read firmware versions\n");
		return;
	}

	/* set fast mode */
	set_sbus_fast_mode(dd);

	/* read version for SBus Master */
	sbus_request(dd, SBUS_MASTER_BROADCAST, 0x02, WRITE_SBUS_RECEIVER, 0);
	sbus_request(dd, SBUS_MASTER_BROADCAST, 0x07, WRITE_SBUS_RECEIVER, 0x1);
	/* wait for interrupt to be processed */
	usleep_range(10000, 11000);
	sbus_vers = sbus_read(dd, SBUS_MASTER_BROADCAST, 0x08, 0x1);
	dd_dev_info(dd, "SBus Master firmware version 0x%08x\n", sbus_vers);

	/* read version for PCIe SerDes */
	all_same = 1;
	pcie_vers[0] = 0;
	for (i = 0; i < NUM_PCIE_SERDES; i++) {
		rcv_addr = pcie_serdes_addrs[dd->hfi1_id][i];
		sbus_request(dd, rcv_addr, 0x03, WRITE_SBUS_RECEIVER, 0);
		/* wait for interrupt to be processed */
		usleep_range(10000, 11000);
		pcie_vers[i] = sbus_read(dd, rcv_addr, 0x04, 0x0);
		if (i > 0 && pcie_vers[0] != pcie_vers[i])
			all_same = 0;
	}

	if (all_same) {
		dd_dev_info(dd, "PCIe SerDes firmware version 0x%x\n",
			    pcie_vers[0]);
	} else {
		dd_dev_warn(dd, "PCIe SerDes do not have the same firmware version\n");
		for (i = 0; i < NUM_PCIE_SERDES; i++) {
			dd_dev_info(dd,
				    "PCIe SerDes lane %d firmware version 0x%x\n",
				    i, pcie_vers[i]);
		}
	}

	/* read version for fabric SerDes */
	all_same = 1;
	fabric_vers[0] = 0;
	for (i = 0; i < NUM_FABRIC_SERDES; i++) {
		rcv_addr = fabric_serdes_addrs[dd->hfi1_id][i];
		sbus_request(dd, rcv_addr, 0x03, WRITE_SBUS_RECEIVER, 0);
		/* wait for interrupt to be processed */
		usleep_range(10000, 11000);
		fabric_vers[i] = sbus_read(dd, rcv_addr, 0x04, 0x0);
		if (i > 0 && fabric_vers[0] != fabric_vers[i])
			all_same = 0;
	}

	if (all_same) {
		dd_dev_info(dd, "Fabric SerDes firmware version 0x%x\n",
			    fabric_vers[0]);
	} else {
		dd_dev_warn(dd, "Fabric SerDes do not have the same firmware version\n");
		for (i = 0; i < NUM_FABRIC_SERDES; i++) {
			dd_dev_info(dd,
				    "Fabric SerDes lane %d firmware version 0x%x\n",
				    i, fabric_vers[i]);
		}
	}

	clear_sbus_fast_mode(dd);
	release_chip_resource(dd, CR_SBUS);
}
