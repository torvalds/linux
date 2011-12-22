/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <linux/acpi.h>

#include "drmP.h"
#include "nouveau_drv.h"

#define MXM_DBG(dev, fmt, args...) NV_DEBUG((dev), "MXM: " fmt, ##args)
#define MXM_MSG(dev, fmt, args...) NV_INFO((dev), "MXM: " fmt, ##args)

static u8 *
mxms_data(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->mxms;

}

static u16
mxms_version(struct drm_device *dev)
{
	u8 *mxms = mxms_data(dev);
	u16 version = (mxms[4] << 8) | mxms[5];
	switch (version ) {
	case 0x0200:
	case 0x0201:
	case 0x0300:
		return version;
	default:
		break;
	}

	MXM_DBG(dev, "unknown version %d.%d\n", mxms[4], mxms[5]);
	return 0x0000;
}

static u16
mxms_headerlen(struct drm_device *dev)
{
	return 8;
}

static u16
mxms_structlen(struct drm_device *dev)
{
	return *(u16 *)&mxms_data(dev)[6];
}

static bool
mxms_checksum(struct drm_device *dev)
{
	u16 size = mxms_headerlen(dev) + mxms_structlen(dev);
	u8 *mxms = mxms_data(dev), sum = 0;
	while (size--)
		sum += *mxms++;
	if (sum) {
		MXM_DBG(dev, "checksum invalid\n");
		return false;
	}
	return true;
}

static bool
mxms_valid(struct drm_device *dev)
{
	u8 *mxms = mxms_data(dev);
	if (*(u32 *)mxms != 0x5f4d584d) {
		MXM_DBG(dev, "signature invalid\n");
		return false;
	}

	if (!mxms_version(dev) || !mxms_checksum(dev))
		return false;

	return true;
}

static bool
mxms_foreach(struct drm_device *dev, u8 types,
	     bool (*exec)(struct drm_device *, u8 *, void *), void *info)
{
	u8 *mxms = mxms_data(dev);
	u8 *desc = mxms + mxms_headerlen(dev);
	u8 *fini = desc + mxms_structlen(dev) - 1;
	while (desc < fini) {
		u8 type = desc[0] & 0x0f;
		u8 headerlen = 0;
		u8 recordlen = 0;
		u8 entries = 0;

		switch (type) {
		case 0: /* Output Device Structure */
			if (mxms_version(dev) >= 0x0300)
				headerlen = 8;
			else
				headerlen = 6;
			break;
		case 1: /* System Cooling Capability Structure */
		case 2: /* Thermal Structure */
		case 3: /* Input Power Structure */
			headerlen = 4;
			break;
		case 4: /* GPIO Device Structure */
			headerlen = 4;
			recordlen = 2;
			entries   = (ROM32(desc[0]) & 0x01f00000) >> 20;
			break;
		case 5: /* Vendor Specific Structure */
			headerlen = 8;
			break;
		case 6: /* Backlight Control Structure */
			if (mxms_version(dev) >= 0x0300) {
				headerlen = 4;
				recordlen = 8;
				entries   = (desc[1] & 0xf0) >> 4;
			} else {
				headerlen = 8;
			}
			break;
		case 7: /* Fan Control Structure */
			headerlen = 8;
			recordlen = 4;
			entries   = desc[1] & 0x07;
			break;
		default:
			MXM_DBG(dev, "unknown descriptor type %d\n", type);
			return false;
		}

		if ((drm_debug & DRM_UT_DRIVER) && (exec == NULL)) {
			static const char * mxms_desc_name[] = {
				"ODS", "SCCS", "TS", "IPS",
				"GSD", "VSS", "BCS", "FCS",
			};
			u8 *dump = desc;
			int i, j;

			MXM_DBG(dev, "%4s: ", mxms_desc_name[type]);
			for (j = headerlen - 1; j >= 0; j--)
				printk("%02x", dump[j]);
			printk("\n");
			dump += headerlen;

			for (i = 0; i < entries; i++, dump += recordlen) {
				MXM_DBG(dev, "      ");
				for (j = recordlen - 1; j >= 0; j--)
					printk("%02x", dump[j]);
				printk("\n");
			}
		}

		if (types & (1 << type)) {
			if (!exec(dev, desc, info))
				return false;
		}

		desc += headerlen + (entries * recordlen);
	}

	return true;
}

static u8 *
mxm_table(struct drm_device *dev, u8 *size)
{
	struct bit_entry x;

	if (bit_table(dev, 'x', &x)) {
		MXM_DBG(dev, "BIT 'x' table not present\n");
		return NULL;
	}

	if (x.version != 1 || x.length < 3) {
		MXM_MSG(dev, "BIT x table %d/%d unknown\n",
			x.version, x.length);
		return NULL;
	}

	*size = x.length;
	return x.data;
}

/* These map MXM v2.x digital connection values to the appropriate SOR/link,
 * hopefully they're correct for all boards within the same chipset...
 *
 * MXM v3.x VBIOS are nicer and provide pointers to these tables.
 */
static u8 nv84_sor_map[16] = {
	0x00, 0x12, 0x22, 0x11, 0x32, 0x31, 0x11, 0x31,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv92_sor_map[16] = {
	0x00, 0x12, 0x22, 0x11, 0x32, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv94_sor_map[16] = {
	0x00, 0x14, 0x24, 0x11, 0x34, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv96_sor_map[16] = {
	0x00, 0x14, 0x24, 0x00, 0x34, 0x00, 0x11, 0x31,
	0x11, 0x31, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv98_sor_map[16] = {
	0x00, 0x14, 0x12, 0x11, 0x00, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8
mxm_sor_map(struct drm_device *dev, u8 conn)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u8 len, *mxm = mxm_table(dev, &len);
	if (mxm && len >= 6) {
		u8 *map = ROMPTR(dev, mxm[4]);
		if (map) {
			if (map[0] == 0x10) {
				if (conn < map[3])
					return map[map[1] + conn];
				return 0x00;
			}

			MXM_MSG(dev, "unknown sor map 0x%02x\n", map[0]);
		}
	}

	if (dev_priv->chipset == 0x84 || dev_priv->chipset == 0x86)
		return nv84_sor_map[conn];
	if (dev_priv->chipset == 0x92)
		return nv92_sor_map[conn];
	if (dev_priv->chipset == 0x94)
		return nv94_sor_map[conn];
	if (dev_priv->chipset == 0x96)
		return nv96_sor_map[conn];
	if (dev_priv->chipset == 0x98)
		return nv98_sor_map[conn];

	MXM_MSG(dev, "missing sor map\n");
	return 0x00;
}

static u8
mxm_ddc_map(struct drm_device *dev, u8 port)
{
	u8 len, *mxm = mxm_table(dev, &len);
	if (mxm && len >= 8) {
		u8 *map = ROMPTR(dev, mxm[6]);
		if (map) {
			if (map[0] == 0x10) {
				if (port < map[3])
					return map[map[1] + port];
				return 0x00;
			}

			MXM_MSG(dev, "unknown ddc map 0x%02x\n", map[0]);
		}
	}

	/* v2.x: directly write port as dcb i2cidx */
	return (port << 4) | port;
}

struct mxms_odev {
	u8 outp_type;
	u8 conn_type;
	u8 ddc_port;
	u8 dig_conn;
};

static void
mxms_output_device(struct drm_device *dev, u8 *pdata, struct mxms_odev *desc)
{
	u64 data = ROM32(pdata[0]);
	if (mxms_version(dev) >= 0x0300)
		data |= (u64)ROM16(pdata[4]) << 32;

	desc->outp_type = (data & 0x00000000000000f0ULL) >> 4;
	desc->ddc_port  = (data & 0x0000000000000f00ULL) >> 8;
	desc->conn_type = (data & 0x000000000001f000ULL) >> 12;
	desc->dig_conn  = (data & 0x0000000000780000ULL) >> 19;
}

struct context {
	u32 *outp;
	struct mxms_odev desc;
};

static bool
mxm_match_tmds_partner(struct drm_device *dev, u8 *data, void *info)
{
	struct context *ctx = info;
	struct mxms_odev desc;

	mxms_output_device(dev, data, &desc);
	if (desc.outp_type == 2 &&
	    desc.dig_conn == ctx->desc.dig_conn)
		return false;
	return true;
}

static bool
mxm_match_dcb(struct drm_device *dev, u8 *data, void *info)
{
	struct context *ctx = info;
	u64 desc = *(u64 *)data;

	mxms_output_device(dev, data, &ctx->desc);

	/* match dcb encoder type to mxm-ods device type */
	if ((ctx->outp[0] & 0x0000000f) != ctx->desc.outp_type)
		return true;

	/* digital output, have some extra stuff to match here, there's a
	 * table in the vbios that provides a mapping from the mxm digital
	 * connection enum values to SOR/link
	 */
	if ((desc & 0x00000000000000f0) >= 0x20) {
		/* check against sor index */
		u8 link = mxm_sor_map(dev, ctx->desc.dig_conn);
		if ((ctx->outp[0] & 0x0f000000) != (link & 0x0f) << 24)
			return true;

		/* check dcb entry has a compatible link field */
		link = (link & 0x30) >> 4;
		if ((link & ((ctx->outp[1] & 0x00000030) >> 4)) != link)
			return true;
	}

	/* mark this descriptor accounted for by setting invalid device type,
	 * except of course some manufactures don't follow specs properly and
	 * we need to avoid killing off the TMDS function on DP connectors
	 * if MXM-SIS is missing an entry for it.
	 */
	data[0] &= ~0xf0;
	if (ctx->desc.outp_type == 6 && ctx->desc.conn_type == 6 &&
	    mxms_foreach(dev, 0x01, mxm_match_tmds_partner, ctx)) {
		data[0] |= 0x20; /* modify descriptor to match TMDS now */
	} else {
		data[0] |= 0xf0;
	}

	return false;
}

static int
mxm_dcb_sanitise_entry(struct drm_device *dev, void *data, int idx, u8 *dcbe)
{
	struct context ctx = { .outp = (u32 *)dcbe };
	u8 type, i2cidx, link;
	u8 *conn;

	/* look for an output device structure that matches this dcb entry.
	 * if one isn't found, disable it.
	 */
	if (mxms_foreach(dev, 0x01, mxm_match_dcb, &ctx)) {
		MXM_DBG(dev, "disable %d: 0x%08x 0x%08x\n",
			idx, ctx.outp[0], ctx.outp[1]);
		ctx.outp[0] |= 0x0000000f;
		return 0;
	}

	/* modify the output's ddc/aux port, there's a pointer to a table
	 * with the mapping from mxm ddc/aux port to dcb i2c_index in the
	 * vbios mxm table
	 */
	i2cidx = mxm_ddc_map(dev, ctx.desc.ddc_port);
	if ((ctx.outp[0] & 0x0000000f) != OUTPUT_DP)
		i2cidx = (i2cidx & 0x0f) << 4;
	else
		i2cidx = (i2cidx & 0xf0);

	if (i2cidx != 0xf0) {
		ctx.outp[0] &= ~0x000000f0;
		ctx.outp[0] |= i2cidx;
	}

	/* override dcb sorconf.link, based on what mxm data says */
	switch (ctx.desc.outp_type) {
	case 0x00: /* Analog CRT */
	case 0x01: /* Analog TV/HDTV */
		break;
	default:
		link = mxm_sor_map(dev, ctx.desc.dig_conn) & 0x30;
		ctx.outp[1] &= ~0x00000030;
		ctx.outp[1] |= link;
		break;
	}

	/* we may need to fixup various other vbios tables based on what
	 * the descriptor says the connector type should be.
	 *
	 * in a lot of cases, the vbios tables will claim DVI-I is possible,
	 * and the mxm data says the connector is really HDMI.  another
	 * common example is DP->eDP.
	 */
	conn = dcb_conn(dev, (ctx.outp[0] & 0x0000f000) >> 12);
	type = conn[0];
	switch (ctx.desc.conn_type) {
	case 0x01: /* LVDS */
		ctx.outp[1] |= 0x00000004; /* use_power_scripts */
		/* XXX: modify default link width in LVDS table */
		break;
	case 0x02: /* HDMI */
		type = DCB_CONNECTOR_HDMI_1;
		break;
	case 0x03: /* DVI-D */
		type = DCB_CONNECTOR_DVI_D;
		break;
	case 0x0e: /* eDP, falls through to DPint */
		ctx.outp[1] |= 0x00010000;
	case 0x07: /* DP internal, wtf is this?? HP8670w */
		ctx.outp[1] |= 0x00000004; /* use_power_scripts? */
		type = DCB_CONNECTOR_eDP;
		break;
	default:
		break;
	}

	if (mxms_version(dev) >= 0x0300)
		conn[0] = type;

	return 0;
}

static bool
mxm_show_unmatched(struct drm_device *dev, u8 *data, void *info)
{
	u64 desc = *(u64 *)data;
	if ((desc & 0xf0) != 0xf0)
		MXM_MSG(dev, "unmatched output device 0x%016llx\n", desc);
	return true;
}

static void
mxm_dcb_sanitise(struct drm_device *dev)
{
	u8 *dcb = dcb_table(dev);
	if (!dcb || dcb[0] != 0x40) {
		MXM_DBG(dev, "unsupported DCB version\n");
		return;
	}

	dcb_outp_foreach(dev, NULL, mxm_dcb_sanitise_entry);
	mxms_foreach(dev, 0x01, mxm_show_unmatched, NULL);
}

static bool
mxm_shadow_rom_fetch(struct nouveau_i2c_chan *i2c, u8 addr,
		     u8 offset, u8 size, u8 *data)
{
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &offset },
		{ .addr = addr, .flags = I2C_M_RD, .len = size, .buf = data, },
	};

	return i2c_transfer(&i2c->adapter, msgs, 2) == 2;
}

static bool
mxm_shadow_rom(struct drm_device *dev, u8 version)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *i2c = NULL;
	u8 i2cidx, mxms[6], addr, size;

	i2cidx = mxm_ddc_map(dev, 1 /* LVDS_DDC */) & 0x0f;
	if (i2cidx < 0x0f)
		i2c = nouveau_i2c_find(dev, i2cidx);
	if (!i2c)
		return false;

	addr = 0x54;
	if (!mxm_shadow_rom_fetch(i2c, addr, 0, 6, mxms)) {
		addr = 0x56;
		if (!mxm_shadow_rom_fetch(i2c, addr, 0, 6, mxms))
			return false;
	}

	dev_priv->mxms = mxms;
	size = mxms_headerlen(dev) + mxms_structlen(dev);
	dev_priv->mxms = kmalloc(size, GFP_KERNEL);

	if (dev_priv->mxms &&
	    mxm_shadow_rom_fetch(i2c, addr, 0, size, dev_priv->mxms))
		return true;

	kfree(dev_priv->mxms);
	dev_priv->mxms = NULL;
	return false;
}

#if defined(CONFIG_ACPI)
static bool
mxm_shadow_dsm(struct drm_device *dev, u8 version)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	static char muid[] = {
		0x00, 0xA4, 0x04, 0x40, 0x7D, 0x91, 0xF2, 0x4C,
		0xB8, 0x9C, 0x79, 0xB6, 0x2F, 0xD5, 0x56, 0x65
	};
	u32 mxms_args[] = { 0x00000000 };
	union acpi_object args[4] = {
		/* _DSM MUID */
		{ .buffer.type = 3,
		  .buffer.length = sizeof(muid),
		  .buffer.pointer = muid,
		},
		/* spec says this can be zero to mean "highest revision", but
		 * of course there's at least one bios out there which fails
		 * unless you pass in exactly the version it supports..
		 */
		{ .integer.type = ACPI_TYPE_INTEGER,
		  .integer.value = (version & 0xf0) << 4 | (version & 0x0f),
		},
		/* MXMS function */
		{ .integer.type = ACPI_TYPE_INTEGER,
		  .integer.value = 0x00000010,
		},
		/* Pointer to MXMS arguments */
		{ .buffer.type = ACPI_TYPE_BUFFER,
		  .buffer.length = sizeof(mxms_args),
		  .buffer.pointer = (char *)mxms_args,
		},
	};
	struct acpi_object_list list = { ARRAY_SIZE(args), args };
	struct acpi_buffer retn = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_handle handle;
	int ret;

	handle = DEVICE_ACPI_HANDLE(&dev->pdev->dev);
	if (!handle)
		return false;

	ret = acpi_evaluate_object(handle, "_DSM", &list, &retn);
	if (ret) {
		MXM_DBG(dev, "DSM MXMS failed: %d\n", ret);
		return false;
	}

	obj = retn.pointer;
	if (obj->type == ACPI_TYPE_BUFFER) {
		dev_priv->mxms = kmemdup(obj->buffer.pointer,
					 obj->buffer.length, GFP_KERNEL);
	} else
	if (obj->type == ACPI_TYPE_INTEGER) {
		MXM_DBG(dev, "DSM MXMS returned 0x%llx\n", obj->integer.value);
	}

	kfree(obj);
	return dev_priv->mxms != NULL;
}
#endif

#if defined(CONFIG_ACPI_WMI) || defined(CONFIG_ACPI_WMI_MODULE)

#define WMI_WMMX_GUID "F6CB5C3C-9CAE-4EBD-B577-931EA32A2CC0"

static bool
mxm_shadow_wmi(struct drm_device *dev, u8 version)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 mxms_args[] = { 0x534D584D /* MXMS */, version, 0 };
	struct acpi_buffer args = { sizeof(mxms_args), mxms_args };
	struct acpi_buffer retn = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	if (!wmi_has_guid(WMI_WMMX_GUID))
		return false;

	status = wmi_evaluate_method(WMI_WMMX_GUID, 0, 0, &args, &retn);
	if (ACPI_FAILURE(status)) {
		MXM_DBG(dev, "WMMX MXMS returned %d\n", status);
		return false;
	}

	obj = retn.pointer;
	if (obj->type == ACPI_TYPE_BUFFER) {
		dev_priv->mxms = kmemdup(obj->buffer.pointer,
					 obj->buffer.length, GFP_KERNEL);
	}

	kfree(obj);
	return dev_priv->mxms != NULL;
}
#endif

struct mxm_shadow_h {
	const char *name;
	bool (*exec)(struct drm_device *, u8 version);
} _mxm_shadow[] = {
	{ "ROM", mxm_shadow_rom },
#if defined(CONFIG_ACPI)
	{ "DSM", mxm_shadow_dsm },
#endif
#if defined(CONFIG_ACPI_WMI) || defined(CONFIG_ACPI_WMI_MODULE)
	{ "WMI", mxm_shadow_wmi },
#endif
	{}
};

static int
mxm_shadow(struct drm_device *dev, u8 version)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct mxm_shadow_h *shadow = _mxm_shadow;
	do {
		MXM_DBG(dev, "checking %s\n", shadow->name);
		if (shadow->exec(dev, version)) {
			if (mxms_valid(dev))
				return 0;
			kfree(dev_priv->mxms);
			dev_priv->mxms = NULL;
		}
	} while ((++shadow)->name);
	return -ENOENT;
}

int
nouveau_mxm_init(struct drm_device *dev)
{
	u8 mxm_size, *mxm = mxm_table(dev, &mxm_size);
	if (!mxm || !mxm[0]) {
		MXM_MSG(dev, "no VBIOS data, nothing to do\n");
		return 0;
	}

	MXM_MSG(dev, "BIOS version %d.%d\n", mxm[0] >> 4, mxm[0] & 0x0f);

	if (mxm_shadow(dev, mxm[0])) {
		MXM_MSG(dev, "failed to locate valid SIS\n");
		return -EINVAL;
	}

	MXM_MSG(dev, "MXMS Version %d.%d\n",
		mxms_version(dev) >> 8, mxms_version(dev) & 0xff);
	mxms_foreach(dev, 0, NULL, NULL);

	if (nouveau_mxmdcb)
		mxm_dcb_sanitise(dev);
	return 0;
}

void
nouveau_mxm_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	kfree(dev_priv->mxms);
	dev_priv->mxms = NULL;
}
