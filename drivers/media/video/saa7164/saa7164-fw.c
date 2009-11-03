/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2009 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/firmware.h>

#include "saa7164.h"

#define SAA7164_REV2_FIRMWARE		"v4l-saa7164-1.0.2.fw"
#define SAA7164_REV2_FIRMWARE_SIZE	3978608

#define SAA7164_REV3_FIRMWARE		"v4l-saa7164-1.0.3.fw"
#define SAA7164_REV3_FIRMWARE_SIZE	3978608

struct fw_header {
	u32	firmwaresize;
	u32	bslsize;
	u32	reserved;
	u32	version;
};

int saa7164_dl_wait_ack(struct saa7164_dev *dev, u32 reg)
{
	u32 timeout = SAA_DEVICE_TIMEOUT;
	while ((saa7164_readl(reg) & 0x01) == 0) {
		timeout -= 10;
		if (timeout == 0) {
			printk(KERN_ERR "%s() timeout (no d/l ack)\n",
				__func__);
			return -EBUSY;
		}
		msleep(100);
	}

	return 0;
}

int saa7164_dl_wait_clr(struct saa7164_dev *dev, u32 reg)
{
	u32 timeout = SAA_DEVICE_TIMEOUT;
	while (saa7164_readl(reg) & 0x01) {
		timeout -= 10;
		if (timeout == 0) {
			printk(KERN_ERR "%s() timeout (no d/l clr)\n",
				__func__);
			return -EBUSY;
		}
		msleep(100);
	}

	return 0;
}

/* TODO: move dlflags into dev-> and change to write/readl/b */
/* TODO: Excessive levels of debug */
int saa7164_downloadimage(struct saa7164_dev *dev, u8 *src, u32 srcsize,
	u32 dlflags, u8 *dst, u32 dstsize)
{
	u32 reg, timeout, offset;
	u8 *srcbuf = NULL;
	int ret;

	u32 dlflag = dlflags;
	u32 dlflag_ack = dlflag + 4;
	u32 drflag = dlflag_ack + 4;
	u32 drflag_ack = drflag + 4;
	u32 bleflag = drflag_ack + 4;

	dprintk(DBGLVL_FW,
		"%s(image=%p, size=%d, flags=0x%x, dst=%p, dstsize=0x%x)\n",
		__func__, src, srcsize, dlflags, dst, dstsize);

	if ((src == 0) || (dst == 0)) {
		ret = -EIO;
		goto out;
	}

	srcbuf = kzalloc(4 * 1048576, GFP_KERNEL);
	if (NULL == srcbuf) {
		ret = -ENOMEM;
		goto out;
	}

	if (srcsize > (4*1048576)) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(srcbuf, src, srcsize);

	dprintk(DBGLVL_FW, "%s() dlflag = 0x%x\n", __func__, dlflag);
	dprintk(DBGLVL_FW, "%s() dlflag_ack = 0x%x\n", __func__, dlflag_ack);
	dprintk(DBGLVL_FW, "%s() drflag = 0x%x\n", __func__, drflag);
	dprintk(DBGLVL_FW, "%s() drflag_ack = 0x%x\n", __func__, drflag_ack);
	dprintk(DBGLVL_FW, "%s() bleflag = 0x%x\n", __func__, bleflag);

	reg = saa7164_readl(dlflag);
	dprintk(DBGLVL_FW, "%s() dlflag (0x%x)= 0x%x\n", __func__, dlflag, reg);
	if (reg == 1)
		dprintk(DBGLVL_FW,
			"%s() Download flag already set, please reboot\n",
			__func__);

	/* Indicate download start */
	saa7164_writel(dlflag, 1);
	ret = saa7164_dl_wait_ack(dev, dlflag_ack);
	if (ret < 0)
		goto out;

	/* Ack download start, then wait for wait */
	saa7164_writel(dlflag, 0);
	ret = saa7164_dl_wait_clr(dev, dlflag_ack);
	if (ret < 0)
		goto out;

	/* Deal with the raw firmware, in the appropriate chunk size */
	for (offset = 0; srcsize > dstsize;
		srcsize -= dstsize, offset += dstsize) {

		dprintk(DBGLVL_FW, "%s() memcpy %d\n", __func__, dstsize);
		memcpy(dst, srcbuf + offset, dstsize);

		/* Flag the data as ready */
		saa7164_writel(drflag, 1);
		ret = saa7164_dl_wait_ack(dev, drflag_ack);
		if (ret < 0)
			goto out;

		/* Wait for indication data was received */
		saa7164_writel(drflag, 0);
		ret = saa7164_dl_wait_clr(dev, drflag_ack);
		if (ret < 0)
			goto out;

	}

	dprintk(DBGLVL_FW, "%s() memcpy(l) %d\n", __func__, dstsize);
	/* Write last block to the device */
	memcpy(dst, srcbuf+offset, srcsize);

	/* Flag the data as ready */
	saa7164_writel(drflag, 1);
	ret = saa7164_dl_wait_ack(dev, drflag_ack);
	if (ret < 0)
		goto out;

	saa7164_writel(drflag, 0);
	timeout = 0;
	while (saa7164_readl(bleflag) != SAA_DEVICE_IMAGE_BOOTING) {
		if (saa7164_readl(bleflag) & SAA_DEVICE_IMAGE_CORRUPT) {
			printk(KERN_ERR "%s() image corrupt\n", __func__);
			ret = -EBUSY;
			goto out;
		}

		if (saa7164_readl(bleflag) & SAA_DEVICE_MEMORY_CORRUPT) {
			printk(KERN_ERR "%s() device memory corrupt\n",
				__func__);
			ret = -EBUSY;
			goto out;
		}

		msleep(10);
		if (timeout++ > 60)
			break;
	}

	printk(KERN_INFO "%s() Image downloaded, booting...\n", __func__);

	ret = saa7164_dl_wait_clr(dev, drflag_ack);
	if (ret < 0)
		goto out;

	printk(KERN_INFO "%s() Image booted successfully.\n", __func__);
	ret = 0;

out:
	kfree(srcbuf);
	return ret;
}

/* TODO: Excessive debug */
/* Load the firmware. Optionally it can be in ROM or newer versions
 * can be on disk, saving the expense of the ROM hardware. */
int saa7164_downloadfirmware(struct saa7164_dev *dev)
{
	/* u32 second_timeout = 60 * SAA_DEVICE_TIMEOUT; */
	u32 tmp, filesize, version, err_flags, first_timeout, fwlength;
	u32 second_timeout, updatebootloader = 1, bootloadersize = 0;
	const struct firmware *fw = NULL;
	struct fw_header *hdr, *boothdr = NULL, *fwhdr;
	u32 bootloaderversion = 0, fwloadersize;
	u8 *bootloaderoffset = NULL, *fwloaderoffset;
	char *fwname;
	int ret;

	dprintk(DBGLVL_FW, "%s()\n", __func__);

	if (saa7164_boards[dev->board].chiprev == SAA7164_CHIP_REV2) {
		fwname = SAA7164_REV2_FIRMWARE;
		fwlength = SAA7164_REV2_FIRMWARE_SIZE;
	} else {
		fwname = SAA7164_REV3_FIRMWARE;
		fwlength = SAA7164_REV3_FIRMWARE_SIZE;
	}

	version = saa7164_getcurrentfirmwareversion(dev);

	if (version == 0x00) {

		second_timeout = 100;
		first_timeout = 100;
		err_flags = saa7164_readl(SAA_BOOTLOADERERROR_FLAGS);
		dprintk(DBGLVL_FW, "%s() err_flags = %x\n",
			__func__, err_flags);

		while (err_flags != SAA_DEVICE_IMAGE_BOOTING) {
			dprintk(DBGLVL_FW, "%s() err_flags = %x\n",
				__func__, err_flags);
			msleep(10);

			if (err_flags & SAA_DEVICE_IMAGE_CORRUPT) {
				printk(KERN_ERR "%s() firmware corrupt\n",
					__func__);
				break;
			}
			if (err_flags & SAA_DEVICE_MEMORY_CORRUPT) {
				printk(KERN_ERR "%s() device memory corrupt\n",
					__func__);
				break;
			}
			if (err_flags & SAA_DEVICE_NO_IMAGE) {
				printk(KERN_ERR "%s() no first image\n",
				__func__);
				break;
			}
			if (err_flags & SAA_DEVICE_IMAGE_SEARCHING) {
				first_timeout -= 10;
				if (first_timeout == 0) {
					printk(KERN_ERR
						"%s() no first image\n",
						__func__);
					break;
				}
			} else if (err_flags & SAA_DEVICE_IMAGE_LOADING) {
				second_timeout -= 10;
				if (second_timeout == 0) {
					printk(KERN_ERR
					"%s() FW load time exceeded\n",
						__func__);
					break;
				}
			} else {
				second_timeout -= 10;
				if (second_timeout == 0) {
					printk(KERN_ERR
					"%s() Unknown bootloader flags 0x%x\n",
						__func__, err_flags);
					break;
				}
			}

			err_flags = saa7164_readl(SAA_BOOTLOADERERROR_FLAGS);
		} /* While != Booting */

		if (err_flags == SAA_DEVICE_IMAGE_BOOTING) {
			dprintk(DBGLVL_FW, "%s() Loader 1 has loaded.\n",
				__func__);
			first_timeout = SAA_DEVICE_TIMEOUT;
			second_timeout = 60 * SAA_DEVICE_TIMEOUT;
			second_timeout = 100;

			err_flags = saa7164_readl(SAA_SECONDSTAGEERROR_FLAGS);
			dprintk(DBGLVL_FW, "%s() err_flags2 = %x\n",
				__func__, err_flags);
			while (err_flags != SAA_DEVICE_IMAGE_BOOTING) {
				dprintk(DBGLVL_FW, "%s() err_flags2 = %x\n",
					__func__, err_flags);
				msleep(10);

				if (err_flags & SAA_DEVICE_IMAGE_CORRUPT) {
					printk(KERN_ERR
						"%s() firmware corrupt\n",
						__func__);
					break;
				}
				if (err_flags & SAA_DEVICE_MEMORY_CORRUPT) {
					printk(KERN_ERR
						"%s() device memory corrupt\n",
						__func__);
					break;
				}
				if (err_flags & SAA_DEVICE_NO_IMAGE) {
					printk(KERN_ERR "%s() no first image\n",
						__func__);
					break;
				}
				if (err_flags & SAA_DEVICE_IMAGE_SEARCHING) {
					first_timeout -= 10;
					if (first_timeout == 0) {
						printk(KERN_ERR
						"%s() no second image\n",
							__func__);
						break;
					}
				} else if (err_flags &
					SAA_DEVICE_IMAGE_LOADING) {
					second_timeout -= 10;
					if (second_timeout == 0) {
						printk(KERN_ERR
						"%s() FW load time exceeded\n",
							__func__);
						break;
					}
				} else {
					second_timeout -= 10;
					if (second_timeout == 0) {
						printk(KERN_ERR
					"%s() Unknown bootloader flags 0x%x\n",
							__func__, err_flags);
						break;
					}
				}

				err_flags =
				saa7164_readl(SAA_SECONDSTAGEERROR_FLAGS);
			} /* err_flags != SAA_DEVICE_IMAGE_BOOTING */

			dprintk(DBGLVL_FW, "%s() Loader flags 1:0x%x 2:0x%x.\n",
				__func__,
				saa7164_readl(SAA_BOOTLOADERERROR_FLAGS),
				saa7164_readl(SAA_SECONDSTAGEERROR_FLAGS));

		} /* err_flags == SAA_DEVICE_IMAGE_BOOTING */

		/* It's possible for both firmwares to have booted,
		 * but that doesn't mean they've finished booting yet.
		 */
		if ((saa7164_readl(SAA_BOOTLOADERERROR_FLAGS) ==
			SAA_DEVICE_IMAGE_BOOTING) &&
			(saa7164_readl(SAA_SECONDSTAGEERROR_FLAGS) ==
			SAA_DEVICE_IMAGE_BOOTING)) {


			dprintk(DBGLVL_FW, "%s() Loader 2 has loaded.\n",
				__func__);

			first_timeout = SAA_DEVICE_TIMEOUT;
			while (first_timeout) {
				msleep(10);

				version =
					saa7164_getcurrentfirmwareversion(dev);
				if (version) {
					dprintk(DBGLVL_FW,
					"%s() All f/w loaded successfully\n",
						__func__);
					break;
				} else {
					first_timeout -= 10;
					if (first_timeout == 0) {
						printk(KERN_ERR
						"%s() FW did not boot\n",
							__func__);
						break;
					}
				}
			}
		}
		version = saa7164_getcurrentfirmwareversion(dev);
	} /* version == 0 */

	/* Has the firmware really booted? */
	if ((saa7164_readl(SAA_BOOTLOADERERROR_FLAGS) ==
		SAA_DEVICE_IMAGE_BOOTING) &&
		(saa7164_readl(SAA_SECONDSTAGEERROR_FLAGS) ==
		SAA_DEVICE_IMAGE_BOOTING) && (version == 0)) {

		printk(KERN_ERR
			"%s() The firmware hung, probably bad firmware\n",
			__func__);

		/* Tell the second stage loader we have a deadlock */
		saa7164_writel(SAA_DEVICE_DEADLOCK_DETECTED_OFFSET,
			SAA_DEVICE_DEADLOCK_DETECTED);

		saa7164_getfirmwarestatus(dev);

		return -ENOMEM;
	}

	dprintk(DBGLVL_FW, "Device has Firmware Version %d.%d.%d.%d\n",
		(version & 0x0000fc00) >> 10,
		(version & 0x000003e0) >> 5,
		(version & 0x0000001f),
		(version & 0xffff0000) >> 16);

	/* Load the firmwware from the disk if required */
	if (version == 0) {

		printk(KERN_INFO "%s() Waiting for firmware upload (%s)\n",
			__func__, fwname);

		ret = request_firmware(&fw, fwname, &dev->pci->dev);
		if (ret) {
			printk(KERN_ERR "%s() Upload failed. "
				"(file not found?)\n", __func__);
			return -ENOMEM;
		}

		printk(KERN_INFO "%s() firmware read %Zu bytes.\n",
			__func__, fw->size);

		if (fw->size != fwlength) {
			printk(KERN_ERR "xc5000: firmware incorrect size\n");
			ret = -ENOMEM;
			goto out;
		}

		printk(KERN_INFO "%s() firmware loaded.\n", __func__);

		hdr = (struct fw_header *)fw->data;
		printk(KERN_INFO "Firmware file header part 1:\n");
		printk(KERN_INFO " .FirmwareSize = 0x%x\n", hdr->firmwaresize);
		printk(KERN_INFO " .BSLSize = 0x%x\n", hdr->bslsize);
		printk(KERN_INFO " .Reserved = 0x%x\n", hdr->reserved);
		printk(KERN_INFO " .Version = 0x%x\n", hdr->version);

		/* Retreive bootloader if reqd */
		if ((hdr->firmwaresize == 0) && (hdr->bslsize == 0))
			/* Second bootloader in the firmware file */
			filesize = hdr->reserved * 16;
		else
			filesize = (hdr->firmwaresize + hdr->bslsize) *
				16 + sizeof(struct fw_header);

		printk(KERN_INFO "%s() SecBootLoader.FileSize = %d\n",
			__func__, filesize);

		/* Get bootloader (if reqd) and firmware header */
		if ((hdr->firmwaresize == 0) && (hdr->bslsize == 0)) {
			/* Second boot loader is required */

			/* Get the loader header */
			boothdr = (struct fw_header *)(fw->data +
				sizeof(struct fw_header));

			bootloaderversion =
				saa7164_readl(SAA_DEVICE_2ND_VERSION);
			dprintk(DBGLVL_FW, "Onboard BootLoader:\n");
			dprintk(DBGLVL_FW, "->Flag 0x%x\n",
				saa7164_readl(SAA_BOOTLOADERERROR_FLAGS));
			dprintk(DBGLVL_FW, "->Ack 0x%x\n",
				saa7164_readl(SAA_DATAREADY_FLAG_ACK));
			dprintk(DBGLVL_FW, "->FW Version 0x%x\n", version);
			dprintk(DBGLVL_FW, "->Loader Version 0x%x\n",
				bootloaderversion);

			if ((saa7164_readl(SAA_BOOTLOADERERROR_FLAGS) ==
				0x03) && (saa7164_readl(SAA_DATAREADY_FLAG_ACK)
				== 0x00) && (version == 0x00)) {

				dprintk(DBGLVL_FW, "BootLoader version in  "
					"rom %d.%d.%d.%d\n",
					(bootloaderversion & 0x0000fc00) >> 10,
					(bootloaderversion & 0x000003e0) >> 5,
					(bootloaderversion & 0x0000001f),
					(bootloaderversion & 0xffff0000) >> 16
					);
				dprintk(DBGLVL_FW, "BootLoader version "
					"in file %d.%d.%d.%d\n",
					(boothdr->version & 0x0000fc00) >> 10,
					(boothdr->version & 0x000003e0) >> 5,
					(boothdr->version & 0x0000001f),
					(boothdr->version & 0xffff0000) >> 16
					);

				if (bootloaderversion == boothdr->version)
					updatebootloader = 0;
			}

			/* Calculate offset to firmware header */
			tmp = (boothdr->firmwaresize + boothdr->bslsize) * 16 +
				(sizeof(struct fw_header) +
				sizeof(struct fw_header));

			fwhdr = (struct fw_header *)(fw->data+tmp);
		} else {
			/* No second boot loader */
			fwhdr = hdr;
		}

		dprintk(DBGLVL_FW, "Firmware version in file %d.%d.%d.%d\n",
			(fwhdr->version & 0x0000fc00) >> 10,
			(fwhdr->version & 0x000003e0) >> 5,
			(fwhdr->version & 0x0000001f),
			(fwhdr->version & 0xffff0000) >> 16
			);

		if (version == fwhdr->version) {
			/* No download, firmware already on board */
			ret = 0;
			goto out;
		}

		if ((hdr->firmwaresize == 0) && (hdr->bslsize == 0)) {
			if (updatebootloader) {
				/* Get ready to upload the bootloader */
				bootloadersize = (boothdr->firmwaresize +
					boothdr->bslsize) * 16 +
					sizeof(struct fw_header);

				bootloaderoffset = (u8 *)(fw->data +
					sizeof(struct fw_header));

				dprintk(DBGLVL_FW, "bootloader d/l starts.\n");
				printk(KERN_INFO "%s() FirmwareSize = 0x%x\n",
					__func__, boothdr->firmwaresize);
				printk(KERN_INFO "%s() BSLSize = 0x%x\n",
					__func__, boothdr->bslsize);
				printk(KERN_INFO "%s() Reserved = 0x%x\n",
					__func__, boothdr->reserved);
				printk(KERN_INFO "%s() Version = 0x%x\n",
					__func__, boothdr->version);
				ret = saa7164_downloadimage(
					dev,
					bootloaderoffset,
					bootloadersize,
					SAA_DOWNLOAD_FLAGS,
					dev->bmmio + SAA_DEVICE_DOWNLOAD_OFFSET,
					SAA_DEVICE_BUFFERBLOCKSIZE);
				if (ret < 0) {
					printk(KERN_ERR
						"bootloader d/l has failed\n");
					goto out;
				}
				dprintk(DBGLVL_FW,
					"bootloader download complete.\n");

			}

			printk(KERN_ERR "starting firmware download(2)\n");
			bootloadersize = (boothdr->firmwaresize +
				boothdr->bslsize) * 16 +
				sizeof(struct fw_header);

			bootloaderoffset =
				(u8 *)(fw->data + sizeof(struct fw_header));

			fwloaderoffset = bootloaderoffset + bootloadersize;

			/* TODO: fix this bounds overrun here with old f/ws */
			fwloadersize = (fwhdr->firmwaresize + fwhdr->bslsize) *
				16 + sizeof(struct fw_header);

			ret = saa7164_downloadimage(
				dev,
				fwloaderoffset,
				fwloadersize,
				SAA_DEVICE_2ND_DOWNLOADFLAG_OFFSET,
				dev->bmmio + SAA_DEVICE_2ND_DOWNLOAD_OFFSET,
				SAA_DEVICE_2ND_BUFFERBLOCKSIZE);
			if (ret < 0) {
				printk(KERN_ERR "firmware download failed\n");
				goto out;
			}
			printk(KERN_ERR "firmware download complete.\n");

		} else {

			/* No bootloader update reqd, download firmware only */
			printk(KERN_ERR "starting firmware download(3)\n");

			ret = saa7164_downloadimage(
				dev,
				(u8 *)fw->data,
				fw->size,
				SAA_DOWNLOAD_FLAGS,
				dev->bmmio + SAA_DEVICE_DOWNLOAD_OFFSET,
				SAA_DEVICE_BUFFERBLOCKSIZE);
			if (ret < 0) {
				printk(KERN_ERR "firmware download failed\n");
				goto out;
			}
			printk(KERN_ERR "firmware download complete.\n");
		}
	}

	ret = 0;

out:
	if (fw)
		release_firmware(fw);

	return ret;
}
