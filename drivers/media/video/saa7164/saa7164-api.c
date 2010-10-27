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

#include <linux/wait.h>
#include <linux/slab.h>

#include "saa7164.h"

int saa7164_api_transition_port(struct saa7164_tsport *port, u8 mode)
{
	int ret;

	ret = saa7164_cmd_send(port->dev, port->hwcfg.unitid, SET_CUR,
		SAA_STATE_CONTROL, sizeof(mode), &mode);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__, ret);

	return ret;
}

int saa7164_api_get_fw_version(struct saa7164_dev *dev, u32 *version)
{
	int ret;

	ret = saa7164_cmd_send(dev, 0, GET_CUR,
		GET_FW_VERSION_CONTROL, sizeof(u32), version);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__, ret);

	return ret;
}

int saa7164_api_read_eeprom(struct saa7164_dev *dev, u8 *buf, int buflen)
{
	u8 reg[] = { 0x0f, 0x00 };

	if (buflen < 128)
		return -ENOMEM;

	/* Assumption: Hauppauge eeprom is at 0xa0 on on bus 0 */
	/* TODO: Pull the details from the boards struct */
	return saa7164_api_i2c_read(&dev->i2c_bus[0], 0xa0 >> 1, sizeof(reg),
		&reg[0], 128, buf);
}


int saa7164_api_configure_port_mpeg2ts(struct saa7164_dev *dev,
	struct saa7164_tsport *port,
	tmComResTSFormatDescrHeader_t *tsfmt)
{
	dprintk(DBGLVL_API, "    bFormatIndex = 0x%x\n", tsfmt->bFormatIndex);
	dprintk(DBGLVL_API, "    bDataOffset  = 0x%x\n", tsfmt->bDataOffset);
	dprintk(DBGLVL_API, "    bPacketLength= 0x%x\n", tsfmt->bPacketLength);
	dprintk(DBGLVL_API, "    bStrideLength= 0x%x\n", tsfmt->bStrideLength);
	dprintk(DBGLVL_API, "    bguid        = (....)\n");

	/* Cache the hardware configuration in the port */

	port->bufcounter = port->hwcfg.BARLocation;
	port->pitch = port->hwcfg.BARLocation + (2 * sizeof(u32));
	port->bufsize = port->hwcfg.BARLocation + (3 * sizeof(u32));
	port->bufoffset = port->hwcfg.BARLocation + (4 * sizeof(u32));
	port->bufptr32l = port->hwcfg.BARLocation +
		(4 * sizeof(u32)) +
		(sizeof(u32) * port->hwcfg.buffercount) + sizeof(u32);
	port->bufptr32h = port->hwcfg.BARLocation +
		(4 * sizeof(u32)) +
		(sizeof(u32) * port->hwcfg.buffercount);
	port->bufptr64 = port->hwcfg.BARLocation +
		(4 * sizeof(u32)) +
		(sizeof(u32) * port->hwcfg.buffercount);
	dprintk(DBGLVL_API, "   = port->hwcfg.BARLocation = 0x%x\n",
		port->hwcfg.BARLocation);

	dprintk(DBGLVL_API, "   = VS_FORMAT_MPEGTS (becomes dev->ts[%d])\n",
		port->nr);

	return 0;
}

int saa7164_api_dump_subdevs(struct saa7164_dev *dev, u8 *buf, int len)
{
	struct saa7164_tsport *port = 0;
	u32 idx, next_offset;
	int i;
	tmComResDescrHeader_t *hdr, *t;
	tmComResExtDevDescrHeader_t *exthdr;
	tmComResPathDescrHeader_t *pathhdr;
	tmComResAntTermDescrHeader_t *anttermhdr;
	tmComResTunerDescrHeader_t *tunerunithdr;
	tmComResDMATermDescrHeader_t *vcoutputtermhdr;
	tmComResTSFormatDescrHeader_t *tsfmt;
	u32 currpath = 0;

	dprintk(DBGLVL_API,
		"%s(?,?,%d) sizeof(tmComResDescrHeader_t) = %d bytes\n",
		__func__, len, (u32)sizeof(tmComResDescrHeader_t));

	for (idx = 0; idx < (len - sizeof(tmComResDescrHeader_t)); ) {

		hdr = (tmComResDescrHeader_t *)(buf + idx);

		if (hdr->type != CS_INTERFACE)
			return SAA_ERR_NOT_SUPPORTED;

		dprintk(DBGLVL_API, "@ 0x%x = \n", idx);
		switch (hdr->subtype) {
		case GENERAL_REQUEST:
			dprintk(DBGLVL_API, " GENERAL_REQUEST\n");
			break;
		case VC_TUNER_PATH:
			dprintk(DBGLVL_API, " VC_TUNER_PATH\n");
			pathhdr = (tmComResPathDescrHeader_t *)(buf + idx);
			dprintk(DBGLVL_API, "  pathid = 0x%x\n",
				pathhdr->pathid);
			currpath = pathhdr->pathid;
			break;
		case VC_INPUT_TERMINAL:
			dprintk(DBGLVL_API, " VC_INPUT_TERMINAL\n");
			anttermhdr =
				(tmComResAntTermDescrHeader_t *)(buf + idx);
			dprintk(DBGLVL_API, "  terminalid   = 0x%x\n",
				anttermhdr->terminalid);
			dprintk(DBGLVL_API, "  terminaltype = 0x%x\n",
				anttermhdr->terminaltype);
			switch (anttermhdr->terminaltype) {
			case ITT_ANTENNA:
				dprintk(DBGLVL_API, "   = ITT_ANTENNA\n");
				break;
			case LINE_CONNECTOR:
				dprintk(DBGLVL_API, "   = LINE_CONNECTOR\n");
				break;
			case SPDIF_CONNECTOR:
				dprintk(DBGLVL_API, "   = SPDIF_CONNECTOR\n");
				break;
			case COMPOSITE_CONNECTOR:
				dprintk(DBGLVL_API,
					"   = COMPOSITE_CONNECTOR\n");
				break;
			case SVIDEO_CONNECTOR:
				dprintk(DBGLVL_API, "   = SVIDEO_CONNECTOR\n");
				break;
			case COMPONENT_CONNECTOR:
				dprintk(DBGLVL_API,
					"   = COMPONENT_CONNECTOR\n");
				break;
			case STANDARD_DMA:
				dprintk(DBGLVL_API, "   = STANDARD_DMA\n");
				break;
			default:
				dprintk(DBGLVL_API, "   = undefined (0x%x)\n",
					anttermhdr->terminaltype);
			}
			dprintk(DBGLVL_API, "  assocterminal= 0x%x\n",
				anttermhdr->assocterminal);
			dprintk(DBGLVL_API, "  iterminal    = 0x%x\n",
				anttermhdr->iterminal);
			dprintk(DBGLVL_API, "  controlsize  = 0x%x\n",
				anttermhdr->controlsize);
			break;
		case VC_OUTPUT_TERMINAL:
			dprintk(DBGLVL_API, " VC_OUTPUT_TERMINAL\n");
			vcoutputtermhdr =
				(tmComResDMATermDescrHeader_t *)(buf + idx);
			dprintk(DBGLVL_API, "  unitid = 0x%x\n",
				vcoutputtermhdr->unitid);
			dprintk(DBGLVL_API, "  terminaltype = 0x%x\n",
				vcoutputtermhdr->terminaltype);
			switch (vcoutputtermhdr->terminaltype) {
			case ITT_ANTENNA:
				dprintk(DBGLVL_API, "   = ITT_ANTENNA\n");
				break;
			case LINE_CONNECTOR:
				dprintk(DBGLVL_API, "   = LINE_CONNECTOR\n");
				break;
			case SPDIF_CONNECTOR:
				dprintk(DBGLVL_API, "   = SPDIF_CONNECTOR\n");
				break;
			case COMPOSITE_CONNECTOR:
				dprintk(DBGLVL_API,
					"   = COMPOSITE_CONNECTOR\n");
				break;
			case SVIDEO_CONNECTOR:
				dprintk(DBGLVL_API, "   = SVIDEO_CONNECTOR\n");
				break;
			case COMPONENT_CONNECTOR:
				dprintk(DBGLVL_API,
					"   = COMPONENT_CONNECTOR\n");
				break;
			case STANDARD_DMA:
				dprintk(DBGLVL_API, "   = STANDARD_DMA\n");
				break;
			default:
				dprintk(DBGLVL_API, "   = undefined (0x%x)\n",
					vcoutputtermhdr->terminaltype);
			}
			dprintk(DBGLVL_API, "  assocterminal= 0x%x\n",
				vcoutputtermhdr->assocterminal);
			dprintk(DBGLVL_API, "  sourceid     = 0x%x\n",
				vcoutputtermhdr->sourceid);
			dprintk(DBGLVL_API, "  iterminal    = 0x%x\n",
				vcoutputtermhdr->iterminal);
			dprintk(DBGLVL_API, "  BARLocation  = 0x%x\n",
				vcoutputtermhdr->BARLocation);
			dprintk(DBGLVL_API, "  flags        = 0x%x\n",
				vcoutputtermhdr->flags);
			dprintk(DBGLVL_API, "  interruptid  = 0x%x\n",
				vcoutputtermhdr->interruptid);
			dprintk(DBGLVL_API, "  buffercount  = 0x%x\n",
				vcoutputtermhdr->buffercount);
			dprintk(DBGLVL_API, "  metadatasize = 0x%x\n",
				vcoutputtermhdr->metadatasize);
			dprintk(DBGLVL_API, "  controlsize  = 0x%x\n",
				vcoutputtermhdr->controlsize);
			dprintk(DBGLVL_API, "  numformats   = 0x%x\n",
				vcoutputtermhdr->numformats);

			t = (tmComResDescrHeader_t *)
				((tmComResDMATermDescrHeader_t *)(buf + idx));
			next_offset = idx + (vcoutputtermhdr->len);
			for (i = 0; i < vcoutputtermhdr->numformats; i++) {
				t = (tmComResDescrHeader_t *)
					(buf + next_offset);
				switch (t->subtype) {
				case VS_FORMAT_MPEG2TS:
					tsfmt =
					(tmComResTSFormatDescrHeader_t *)t;
					if (currpath == 1)
						port = &dev->ts1;
					else
						port = &dev->ts2;
					memcpy(&port->hwcfg, vcoutputtermhdr,
						sizeof(*vcoutputtermhdr));
					saa7164_api_configure_port_mpeg2ts(dev,
						port, tsfmt);
					break;
				case VS_FORMAT_MPEG2PS:
					dprintk(DBGLVL_API,
						"   = VS_FORMAT_MPEG2PS\n");
					break;
				case VS_FORMAT_VBI:
					dprintk(DBGLVL_API,
						"   = VS_FORMAT_VBI\n");
					break;
				case VS_FORMAT_RDS:
					dprintk(DBGLVL_API,
						"   = VS_FORMAT_RDS\n");
					break;
				case VS_FORMAT_UNCOMPRESSED:
					dprintk(DBGLVL_API,
					"   = VS_FORMAT_UNCOMPRESSED\n");
					break;
				case VS_FORMAT_TYPE:
					dprintk(DBGLVL_API,
						"   = VS_FORMAT_TYPE\n");
					break;
				default:
					dprintk(DBGLVL_API,
						"   = undefined (0x%x)\n",
						t->subtype);
				}
				next_offset += t->len;
			}

			break;
		case TUNER_UNIT:
			dprintk(DBGLVL_API, " TUNER_UNIT\n");
			tunerunithdr =
				(tmComResTunerDescrHeader_t *)(buf + idx);
			dprintk(DBGLVL_API, "  unitid = 0x%x\n",
				tunerunithdr->unitid);
			dprintk(DBGLVL_API, "  sourceid = 0x%x\n",
				tunerunithdr->sourceid);
			dprintk(DBGLVL_API, "  iunit = 0x%x\n",
				tunerunithdr->iunit);
			dprintk(DBGLVL_API, "  tuningstandards = 0x%x\n",
				tunerunithdr->tuningstandards);
			dprintk(DBGLVL_API, "  controlsize = 0x%x\n",
				tunerunithdr->controlsize);
			dprintk(DBGLVL_API, "  controls = 0x%x\n",
				tunerunithdr->controls);
			break;
		case VC_SELECTOR_UNIT:
			dprintk(DBGLVL_API, " VC_SELECTOR_UNIT\n");
			break;
		case VC_PROCESSING_UNIT:
			dprintk(DBGLVL_API, " VC_PROCESSING_UNIT\n");
			break;
		case FEATURE_UNIT:
			dprintk(DBGLVL_API, " FEATURE_UNIT\n");
			break;
		case ENCODER_UNIT:
			dprintk(DBGLVL_API, " ENCODER_UNIT\n");
			break;
		case EXTENSION_UNIT:
			dprintk(DBGLVL_API, " EXTENSION_UNIT\n");
			exthdr = (tmComResExtDevDescrHeader_t *)(buf + idx);
			dprintk(DBGLVL_API, "  unitid = 0x%x\n",
				exthdr->unitid);
			dprintk(DBGLVL_API, "  deviceid = 0x%x\n",
				exthdr->deviceid);
			dprintk(DBGLVL_API, "  devicetype = 0x%x\n",
				exthdr->devicetype);
			if (exthdr->devicetype & 0x1)
				dprintk(DBGLVL_API, "   = Decoder Device\n");
			if (exthdr->devicetype & 0x2)
				dprintk(DBGLVL_API, "   = GPIO Source\n");
			if (exthdr->devicetype & 0x4)
				dprintk(DBGLVL_API, "   = Video Decoder\n");
			if (exthdr->devicetype & 0x8)
				dprintk(DBGLVL_API, "   = Audio Decoder\n");
			if (exthdr->devicetype & 0x20)
				dprintk(DBGLVL_API, "   = Crossbar\n");
			if (exthdr->devicetype & 0x40)
				dprintk(DBGLVL_API, "   = Tuner\n");
			if (exthdr->devicetype & 0x80)
				dprintk(DBGLVL_API, "   = IF PLL\n");
			if (exthdr->devicetype & 0x100)
				dprintk(DBGLVL_API, "   = Demodulator\n");
			if (exthdr->devicetype & 0x200)
				dprintk(DBGLVL_API, "   = RDS Decoder\n");
			if (exthdr->devicetype & 0x400)
				dprintk(DBGLVL_API, "   = Encoder\n");
			if (exthdr->devicetype & 0x800)
				dprintk(DBGLVL_API, "   = IR Decoder\n");
			if (exthdr->devicetype & 0x1000)
				dprintk(DBGLVL_API, "   = EEPROM\n");
			if (exthdr->devicetype & 0x2000)
				dprintk(DBGLVL_API,
					"   = VBI Decoder\n");
			if (exthdr->devicetype & 0x10000)
				dprintk(DBGLVL_API,
					"   = Streaming Device\n");
			if (exthdr->devicetype & 0x20000)
				dprintk(DBGLVL_API,
					"   = DRM Device\n");
			if (exthdr->devicetype & 0x40000000)
				dprintk(DBGLVL_API,
					"   = Generic Device\n");
			if (exthdr->devicetype & 0x80000000)
				dprintk(DBGLVL_API,
					"   = Config Space Device\n");
			dprintk(DBGLVL_API, "  numgpiopins = 0x%x\n",
				exthdr->numgpiopins);
			dprintk(DBGLVL_API, "  numgpiogroups = 0x%x\n",
				exthdr->numgpiogroups);
			dprintk(DBGLVL_API, "  controlsize = 0x%x\n",
				exthdr->controlsize);
			break;
		case PVC_INFRARED_UNIT:
			dprintk(DBGLVL_API, " PVC_INFRARED_UNIT\n");
			break;
		case DRM_UNIT:
			dprintk(DBGLVL_API, " DRM_UNIT\n");
			break;
		default:
			dprintk(DBGLVL_API, "default %d\n", hdr->subtype);
		}

		dprintk(DBGLVL_API, " 1.%x\n", hdr->len);
		dprintk(DBGLVL_API, " 2.%x\n", hdr->type);
		dprintk(DBGLVL_API, " 3.%x\n", hdr->subtype);
		dprintk(DBGLVL_API, " 4.%x\n", hdr->unitid);

		idx += hdr->len;
	}

	return 0;
}

int saa7164_api_enum_subdevs(struct saa7164_dev *dev)
{
	int ret;
	u32 buflen = 0;
	u8 *buf;

	dprintk(DBGLVL_API, "%s()\n", __func__);

	/* Get the total descriptor length */
	ret = saa7164_cmd_send(dev, 0, GET_LEN,
		GET_DESCRIPTORS_CONTROL, sizeof(buflen), &buflen);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__, ret);

	dprintk(DBGLVL_API, "%s() total descriptor size = %d bytes.\n",
		__func__, buflen);

	/* Allocate enough storage for all of the descs */
	buf = kzalloc(buflen, GFP_KERNEL);
	if (buf == NULL)
		return SAA_ERR_NO_RESOURCES;

	/* Retrieve them */
	ret = saa7164_cmd_send(dev, 0, GET_CUR,
		GET_DESCRIPTORS_CONTROL, buflen, buf);
	if (ret != SAA_OK) {
		printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__, ret);
		goto out;
	}

	if (saa_debug & DBGLVL_API)
		saa7164_dumphex16(dev, buf, (buflen/16)*16);

	saa7164_api_dump_subdevs(dev, buf, buflen);

out:
	kfree(buf);
	return ret;
}

int saa7164_api_i2c_read(struct saa7164_i2c *bus, u8 addr, u32 reglen, u8 *reg,
	u32 datalen, u8 *data)
{
	struct saa7164_dev *dev = bus->dev;
	u16 len = 0;
	int unitid;
	u32 regval;
	u8 buf[256];
	int ret;

	dprintk(DBGLVL_API, "%s()\n", __func__);

	if (reglen > 4)
		return -EIO;

	if (reglen == 1)
		regval = *(reg);
	else
	if (reglen == 2)
		regval = ((*(reg) << 8) || *(reg+1));
	else
	if (reglen == 3)
		regval = ((*(reg) << 16) | (*(reg+1) << 8) | *(reg+2));
	else
	if (reglen == 4)
		regval = ((*(reg) << 24) | (*(reg+1) << 16) |
			(*(reg+2) << 8) | *(reg+3));

	/* Prepare the send buffer */
	/* Bytes 00-03 source register length
	 *       04-07 source bytes to read
	 *       08... register address
	 */
	memset(buf, 0, sizeof(buf));
	memcpy((buf + 2 * sizeof(u32) + 0), reg, reglen);
	*((u32 *)(buf + 0 * sizeof(u32))) = reglen;
	*((u32 *)(buf + 1 * sizeof(u32))) = datalen;

	unitid = saa7164_i2caddr_to_unitid(bus, addr);
	if (unitid < 0) {
		printk(KERN_ERR
			"%s() error, cannot translate regaddr 0x%x to unitid\n",
			__func__, addr);
		return -EIO;
	}

	ret = saa7164_cmd_send(bus->dev, unitid, GET_LEN,
		EXU_REGISTER_ACCESS_CONTROL, sizeof(len), &len);
	if (ret != SAA_OK) {
		printk(KERN_ERR "%s() error, ret(1) = 0x%x\n", __func__, ret);
		return -EIO;
	}

	dprintk(DBGLVL_API, "%s() len = %d bytes\n", __func__, len);

	if (saa_debug & DBGLVL_I2C)
		saa7164_dumphex16(dev, buf, 2 * 16);

	ret = saa7164_cmd_send(bus->dev, unitid, GET_CUR,
		EXU_REGISTER_ACCESS_CONTROL, len, &buf);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret(2) = 0x%x\n", __func__, ret);
	else {
		if (saa_debug & DBGLVL_I2C)
			saa7164_dumphex16(dev, buf, sizeof(buf));
		memcpy(data, (buf + 2 * sizeof(u32) + reglen), datalen);
	}

	return ret == SAA_OK ? 0 : -EIO;
}

/* For a given 8 bit i2c address device, write the buffer */
int saa7164_api_i2c_write(struct saa7164_i2c *bus, u8 addr, u32 datalen,
	u8 *data)
{
	struct saa7164_dev *dev = bus->dev;
	u16 len = 0;
	int unitid;
	int reglen;
	u8 buf[256];
	int ret;

	dprintk(DBGLVL_API, "%s()\n", __func__);

	if ((datalen == 0) || (datalen > 232))
		return -EIO;

	memset(buf, 0, sizeof(buf));

	unitid = saa7164_i2caddr_to_unitid(bus, addr);
	if (unitid < 0) {
		printk(KERN_ERR
			"%s() error, cannot translate regaddr 0x%x to unitid\n",
			__func__, addr);
		return -EIO;
	}

	reglen = saa7164_i2caddr_to_reglen(bus, addr);
	if (reglen < 0) {
		printk(KERN_ERR
			"%s() error, cannot translate regaddr to reglen\n",
			__func__);
		return -EIO;
	}

	ret = saa7164_cmd_send(bus->dev, unitid, GET_LEN,
		EXU_REGISTER_ACCESS_CONTROL, sizeof(len), &len);
	if (ret != SAA_OK) {
		printk(KERN_ERR "%s() error, ret(1) = 0x%x\n", __func__, ret);
		return -EIO;
	}

	dprintk(DBGLVL_API, "%s() len = %d bytes\n", __func__, len);

	/* Prepare the send buffer */
	/* Bytes 00-03 dest register length
	 *       04-07 dest bytes to write
	 *       08... register address
	 */
	*((u32 *)(buf + 0 * sizeof(u32))) = reglen;
	*((u32 *)(buf + 1 * sizeof(u32))) = datalen - reglen;
	memcpy((buf + 2 * sizeof(u32)), data, datalen);

	if (saa_debug & DBGLVL_I2C)
		saa7164_dumphex16(dev, buf, sizeof(buf));

	ret = saa7164_cmd_send(bus->dev, unitid, SET_CUR,
		EXU_REGISTER_ACCESS_CONTROL, len, &buf);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret(2) = 0x%x\n", __func__, ret);

	return ret == SAA_OK ? 0 : -EIO;
}


int saa7164_api_modify_gpio(struct saa7164_dev *dev, u8 unitid,
	u8 pin, u8 state)
{
	int ret;
	tmComResGPIO_t t;

	dprintk(DBGLVL_API, "%s(0x%x, %d, %d)\n",
		__func__, unitid, pin, state);

	if ((pin > 7) || (state > 2))
		return SAA_ERR_BAD_PARAMETER;

	t.pin = pin;
	t.state = state;

	ret = saa7164_cmd_send(dev, unitid, SET_CUR,
		EXU_GPIO_CONTROL, sizeof(t), &t);
	if (ret != SAA_OK)
		printk(KERN_ERR "%s() error, ret = 0x%x\n",
			__func__, ret);

	return ret;
}

int saa7164_api_set_gpiobit(struct saa7164_dev *dev, u8 unitid,
	u8 pin)
{
	return saa7164_api_modify_gpio(dev, unitid, pin, 1);
}

int saa7164_api_clear_gpiobit(struct saa7164_dev *dev, u8 unitid,
	u8 pin)
{
	return saa7164_api_modify_gpio(dev, unitid, pin, 0);
}



