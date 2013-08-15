/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/fmc.h>
#include <asm/unaligned.h>

/*
 * This module uses the firmware loader to program the whole or part
 * of the FMC eeprom. The meat is in the _run functions.  However, no
 * default file name is provided, to avoid accidental mishaps. Also,
 * you must pass the busid argument
 */
static struct fmc_driver fwe_drv;

FMC_PARAM_BUSID(fwe_drv);

/* The "file=" is like the generic "gateware=" used elsewhere */
static char *fwe_file[FMC_MAX_CARDS];
static int fwe_file_n;
module_param_array_named(file, fwe_file, charp, &fwe_file_n, 444);

static int fwe_run_tlv(struct fmc_device *fmc, const struct firmware *fw,
	int write)
{
	const uint8_t *p = fw->data;
	int len = fw->size;
	uint16_t thislen, thisaddr;
	int err;

	/* format is: 'w' addr16 len16 data... */
	while (len > 5) {
		thisaddr = get_unaligned_le16(p+1);
		thislen = get_unaligned_le16(p+3);
		if (p[0] != 'w' || thislen + 5 > len) {
			dev_err(&fmc->dev, "invalid tlv at offset %ti\n",
				p - fw->data);
			return -EINVAL;
		}
		err = 0;
		if (write) {
			dev_info(&fmc->dev, "write %i bytes at 0x%04x\n",
				 thislen, thisaddr);
			err = fmc->op->write_ee(fmc, thisaddr, p + 5, thislen);
		}
		if (err < 0) {
			dev_err(&fmc->dev, "write failure @0x%04x\n",
				thisaddr);
			return err;
		}
		p += 5 + thislen;
		len -= 5 + thislen;
	}
	if (write)
		dev_info(&fmc->dev, "write_eeprom: success\n");
	return 0;
}

static int fwe_run_bin(struct fmc_device *fmc, const struct firmware *fw)
{
	int ret;

	dev_info(&fmc->dev, "programming %zi bytes\n", fw->size);
	ret = fmc->op->write_ee(fmc, 0, (void *)fw->data, fw->size);
	if (ret < 0) {
		dev_info(&fmc->dev, "write_eeprom: error %i\n", ret);
		return ret;
	}
	dev_info(&fmc->dev, "write_eeprom: success\n");
	return 0;
}

static int fwe_run(struct fmc_device *fmc, const struct firmware *fw, char *s)
{
	char *last4 = s + strlen(s) - 4;
	int err;

	if (!strcmp(last4, ".bin"))
		return fwe_run_bin(fmc, fw);
	if (!strcmp(last4, ".tlv")) {
		err = fwe_run_tlv(fmc, fw, 0);
		if (!err)
			err = fwe_run_tlv(fmc, fw, 1);
		return err;
	}
	dev_err(&fmc->dev, "invalid file name \"%s\"\n", s);
	return -EINVAL;
}

/*
 * Programming is done at probe time. Morever, only those listed with
 * busid= are programmed.
 * card is probed for, only one is programmed. Unfortunately, it's
 * difficult to know in advance when probing the first card if others
 * are there.
 */
int fwe_probe(struct fmc_device *fmc)
{
	int err, index = 0;
	const struct firmware *fw;
	struct device *dev = &fmc->dev;
	char *s;

	if (!fwe_drv.busid_n) {
		dev_err(dev, "%s: no busid passed, refusing all cards\n",
			KBUILD_MODNAME);
		return -ENODEV;
	}
	if (fmc->op->validate)
		index = fmc->op->validate(fmc, &fwe_drv);
	if (index < 0) {
		pr_err("%s: refusing device \"%s\"\n", KBUILD_MODNAME,
		       dev_name(dev));
		return -ENODEV;
	}
	if (index >= fwe_file_n) {
		pr_err("%s: no filename for device index %i\n",
			KBUILD_MODNAME, index);
		return -ENODEV;
	}
	s = fwe_file[index];
	if (!s) {
		pr_err("%s: no filename for \"%s\" not programming\n",
		       KBUILD_MODNAME, dev_name(dev));
		return -ENOENT;
	}
	err = request_firmware(&fw, s, dev);
	if (err < 0) {
		dev_err(&fmc->dev, "request firmware \"%s\": error %i\n",
			s, err);
		return err;
	}
	fwe_run(fmc, fw, s);
	release_firmware(fw);
	return 0;
}

int fwe_remove(struct fmc_device *fmc)
{
	return 0;
}

static struct fmc_driver fwe_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = fwe_probe,
	.remove = fwe_remove,
	/* no table, as the current match just matches everything */
};

static int fwe_init(void)
{
	int ret;

	ret = fmc_driver_register(&fwe_drv);
	return ret;
}

static void fwe_exit(void)
{
	fmc_driver_unregister(&fwe_drv);
}

module_init(fwe_init);
module_exit(fwe_exit);

MODULE_LICENSE("GPL");
