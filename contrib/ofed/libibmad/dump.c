/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2007 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009-2011 Mellanox Technologies LTD.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <infiniband/mad.h>

void mad_dump_int(char *buf, int bufsz, void *val, int valsz)
{
	switch (valsz) {
	case 1:
		snprintf(buf, bufsz, "%d", *(uint32_t *) val & 0xff);
		break;
	case 2:
		snprintf(buf, bufsz, "%d", *(uint32_t *) val & 0xffff);
		break;
	case 3:
	case 4:
		snprintf(buf, bufsz, "%d", *(uint32_t *) val);
		break;
	case 5:
	case 6:
	case 7:
	case 8:
		snprintf(buf, bufsz, "%" PRIu64, *(uint64_t *) val);
		break;
	default:
		IBWARN("bad int sz %d", valsz);
		buf[0] = 0;
	}
}

void mad_dump_uint(char *buf, int bufsz, void *val, int valsz)
{
	switch (valsz) {
	case 1:
		snprintf(buf, bufsz, "%u", *(uint32_t *) val & 0xff);
		break;
	case 2:
		snprintf(buf, bufsz, "%u", *(uint32_t *) val & 0xffff);
		break;
	case 3:
	case 4:
		snprintf(buf, bufsz, "%u", *(uint32_t *) val);
		break;
	case 5:
	case 6:
	case 7:
	case 8:
		snprintf(buf, bufsz, "%" PRIu64, *(uint64_t *) val);
		break;
	default:
		IBWARN("bad int sz %u", valsz);
		buf[0] = 0;
	}
}

void mad_dump_hex(char *buf, int bufsz, void *val, int valsz)
{
	switch (valsz) {
	case 1:
		snprintf(buf, bufsz, "0x%02x", *(uint32_t *) val & 0xff);
		break;
	case 2:
		snprintf(buf, bufsz, "0x%04x", *(uint32_t *) val & 0xffff);
		break;
	case 3:
		snprintf(buf, bufsz, "0x%06x", *(uint32_t *) val & 0xffffff);
		break;
	case 4:
		snprintf(buf, bufsz, "0x%08x", *(uint32_t *) val);
		break;
	case 5:
		snprintf(buf, bufsz, "0x%010" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffULL);
		break;
	case 6:
		snprintf(buf, bufsz, "0x%012" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffffULL);
		break;
	case 7:
		snprintf(buf, bufsz, "0x%014" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffffffULL);
		break;
	case 8:
		snprintf(buf, bufsz, "0x%016" PRIx64, *(uint64_t *) val);
		break;
	default:
		IBWARN("bad int sz %d", valsz);
		buf[0] = 0;
	}
}

void mad_dump_rhex(char *buf, int bufsz, void *val, int valsz)
{
	switch (valsz) {
	case 1:
		snprintf(buf, bufsz, "%02x", *(uint32_t *) val & 0xff);
		break;
	case 2:
		snprintf(buf, bufsz, "%04x", *(uint32_t *) val & 0xffff);
		break;
	case 3:
		snprintf(buf, bufsz, "%06x", *(uint32_t *) val & 0xffffff);
		break;
	case 4:
		snprintf(buf, bufsz, "%08x", *(uint32_t *) val);
		break;
	case 5:
		snprintf(buf, bufsz, "%010" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffULL);
		break;
	case 6:
		snprintf(buf, bufsz, "%012" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffffULL);
		break;
	case 7:
		snprintf(buf, bufsz, "%014" PRIx64,
			 *(uint64_t *) val & (uint64_t) 0xffffffffffffffULL);
		break;
	case 8:
		snprintf(buf, bufsz, "%016" PRIx64, *(uint64_t *) val);
		break;
	default:
		IBWARN("bad int sz %d", valsz);
		buf[0] = 0;
	}
}

void mad_dump_linkwidth(char *buf, int bufsz, void *val, int valsz)
{
	int width = *(int *)val;

	switch (width) {
	case 1:
		snprintf(buf, bufsz, "1X");
		break;
	case 2:
		snprintf(buf, bufsz, "4X");
		break;
	case 4:
		snprintf(buf, bufsz, "8X");
		break;
	case 8:
		snprintf(buf, bufsz, "12X");
		break;
	case 16:
		snprintf(buf, bufsz, "2X");
		break;
	default:
		IBWARN("bad width %d", width);
		snprintf(buf, bufsz, "undefined (%d)", width);
		break;
	}
}

static void dump_linkwidth(char *buf, int bufsz, int width)
{
	int n = 0;

	if (width & 0x1)
		n += snprintf(buf + n, bufsz - n, "1X or ");
	if (n < bufsz && (width & 0x2))
		n += snprintf(buf + n, bufsz - n, "4X or ");
	if (n < bufsz && (width & 0x4))
		n += snprintf(buf + n, bufsz - n, "8X or ");
	if (n < bufsz && (width & 0x8))
		n += snprintf(buf + n, bufsz - n, "12X or ");
	if (n < bufsz && (width & 0x10))
		n += snprintf(buf + n, bufsz - n, "2X or ");

	if (n >= bufsz)
		return;
	else if (width == 0 || (width >> 5))
		snprintf(buf + n, bufsz - n, "undefined (%d)", width);
	else if (bufsz > 3)
		buf[n - 4] = '\0';
}

void mad_dump_linkwidthsup(char *buf, int bufsz, void *val, int valsz)
{
	int width = *(int *)val;

	dump_linkwidth(buf, bufsz, width);

	switch (width) {
	case 1:
	case 3:
	case 7:
	case 11:
	case 15:
	case 17:
	case 19:
	case 23:
	case 27:
	case 31:
		break;

	default:
		if (!(width >> 5))
			snprintf(buf + strlen(buf), bufsz - strlen(buf),
				 " (IBA extension)");
		break;
	}
}

void mad_dump_linkwidthen(char *buf, int bufsz, void *val, int valsz)
{
	int width = *(int *)val;

	dump_linkwidth(buf, bufsz, width);
}

void mad_dump_linkspeed(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	switch (speed) {
	case 0:
		snprintf(buf, bufsz, "Extended speed");
		break;
	case 1:
		snprintf(buf, bufsz, "2.5 Gbps");
		break;
	case 2:
		snprintf(buf, bufsz, "5.0 Gbps");
		break;
	case 4:
		snprintf(buf, bufsz, "10.0 Gbps");
		break;
	default:
		snprintf(buf, bufsz, "undefined (%d)", speed);
		break;
	}
}

static void dump_linkspeed(char *buf, int bufsz, int speed)
{
	int n = 0;

	if (speed & 0x1)
		n += snprintf(buf + n, bufsz - n, "2.5 Gbps or ");
	if (n < bufsz && (speed & 0x2))
		n += snprintf(buf + n, bufsz - n, "5.0 Gbps or ");
	if (n < bufsz && (speed & 0x4))
		n += snprintf(buf + n, bufsz - n, "10.0 Gbps or ");

	if (n >= bufsz)
		return;
	else if (speed == 0 || (speed >> 3)) {
		n += snprintf(buf + n, bufsz - n, "undefined (%d)", speed);
		if (n >= bufsz)
			return;
	} else if (bufsz > 3) {
		buf[n - 4] = '\0';
		n -= 4;
	}

	switch (speed) {
	case 1:
	case 3:
	case 5:
	case 7:
		break;
	default:
		if (!(speed >> 3))
			snprintf(buf + n, bufsz - n, " (IBA extension)");
		break;
	}
}

void mad_dump_linkspeedsup(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	dump_linkspeed(buf, bufsz, speed);
}

void mad_dump_linkspeeden(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	dump_linkspeed(buf, bufsz, speed);
}

void mad_dump_linkspeedext(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	switch (speed) {
	case 0:
		snprintf(buf, bufsz, "No Extended Speed");
		break;
	case 1:
		snprintf(buf, bufsz, "14.0625 Gbps");
		break;
	case 2:
		snprintf(buf, bufsz, "25.78125 Gbps");
		break;
	default:
		snprintf(buf, bufsz, "undefined (%d)", speed);
		break;
	}
}

static void dump_linkspeedext(char *buf, int bufsz, int speed)
{
	int n = 0;

	if (speed == 0) {
		sprintf(buf, "%d", speed);
		return;
	}

	if (speed & 0x1)
		n += snprintf(buf + n, bufsz - n, "14.0625 Gbps or ");
	if (n < bufsz && speed & 0x2)
		n += snprintf(buf + n, bufsz - n, "25.78125 Gbps or ");
	if (n >= bufsz) {
		if (bufsz > 3)
			buf[n - 4] = '\0';
		return;
	}

	if (speed >> 2) {
		n += snprintf(buf + n, bufsz - n, "undefined (%d)", speed);
		return;
	} else if (bufsz > 3)
		buf[n - 4] = '\0';
}

void mad_dump_linkspeedextsup(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	dump_linkspeedext(buf, bufsz, speed);
}

void mad_dump_linkspeedexten(char *buf, int bufsz, void *val, int valsz)
{
	int speed = *(int *)val;

	if (speed == 30) {
		sprintf(buf, "%s", "Extended link speeds disabled");
		return;
	}
	dump_linkspeedext(buf, bufsz, speed);
}

void mad_dump_portstate(char *buf, int bufsz, void *val, int valsz)
{
	int state = *(int *)val;

	switch (state) {
	case 0:
		snprintf(buf, bufsz, "NoChange");
		break;
	case 1:
		snprintf(buf, bufsz, "Down");
		break;
	case 2:
		snprintf(buf, bufsz, "Initialize");
		break;
	case 3:
		snprintf(buf, bufsz, "Armed");
		break;
	case 4:
		snprintf(buf, bufsz, "Active");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", state);
	}
}

void mad_dump_linkdowndefstate(char *buf, int bufsz, void *val, int valsz)
{
	int state = *(int *)val;

	switch (state) {
	case 0:
		snprintf(buf, bufsz, "NoChange");
		break;
	case 1:
		snprintf(buf, bufsz, "Sleep");
		break;
	case 2:
		snprintf(buf, bufsz, "Polling");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", state);
		break;
	}
}

void mad_dump_physportstate(char *buf, int bufsz, void *val, int valsz)
{
	int state = *(int *)val;

	switch (state) {
	case 0:
		snprintf(buf, bufsz, "NoChange");
		break;
	case 1:
		snprintf(buf, bufsz, "Sleep");
		break;
	case 2:
		snprintf(buf, bufsz, "Polling");
		break;
	case 3:
		snprintf(buf, bufsz, "Disabled");
		break;
	case 4:
		snprintf(buf, bufsz, "PortConfigurationTraining");
		break;
	case 5:
		snprintf(buf, bufsz, "LinkUp");
		break;
	case 6:
		snprintf(buf, bufsz, "LinkErrorRecovery");
		break;
	case 7:
		snprintf(buf, bufsz, "PhyTest");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", state);
	}
}

void mad_dump_mtu(char *buf, int bufsz, void *val, int valsz)
{
	int mtu = *(int *)val;

	switch (mtu) {
	case 1:
		snprintf(buf, bufsz, "256");
		break;
	case 2:
		snprintf(buf, bufsz, "512");
		break;
	case 3:
		snprintf(buf, bufsz, "1024");
		break;
	case 4:
		snprintf(buf, bufsz, "2048");
		break;
	case 5:
		snprintf(buf, bufsz, "4096");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", mtu);
	}
}

void mad_dump_vlcap(char *buf, int bufsz, void *val, int valsz)
{
	int vlcap = *(int *)val;

	switch (vlcap) {
	case 1:
		snprintf(buf, bufsz, "VL0");
		break;
	case 2:
		snprintf(buf, bufsz, "VL0-1");
		break;
	case 3:
		snprintf(buf, bufsz, "VL0-3");
		break;
	case 4:
		snprintf(buf, bufsz, "VL0-7");
		break;
	case 5:
		snprintf(buf, bufsz, "VL0-14");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", vlcap);
	}
}

void mad_dump_opervls(char *buf, int bufsz, void *val, int valsz)
{
	int opervls = *(int *)val;

	switch (opervls) {
	case 0:
		snprintf(buf, bufsz, "No change");
		break;
	case 1:
		snprintf(buf, bufsz, "VL0");
		break;
	case 2:
		snprintf(buf, bufsz, "VL0-1");
		break;
	case 3:
		snprintf(buf, bufsz, "VL0-3");
		break;
	case 4:
		snprintf(buf, bufsz, "VL0-7");
		break;
	case 5:
		snprintf(buf, bufsz, "VL0-14");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)", opervls);
	}
}

void mad_dump_portcapmask(char *buf, int bufsz, void *val, int valsz)
{
	unsigned mask = *(unsigned *)val;
	char *s = buf;

	s += sprintf(s, "0x%x\n", mask);
	if (mask & (1 << 1))
		s += sprintf(s, "\t\t\t\tIsSM\n");
	if (mask & (1 << 2))
		s += sprintf(s, "\t\t\t\tIsNoticeSupported\n");
	if (mask & (1 << 3))
		s += sprintf(s, "\t\t\t\tIsTrapSupported\n");
	if (mask & (1 << 4))
		s += sprintf(s, "\t\t\t\tIsOptionalIPDSupported\n");
	if (mask & (1 << 5))
		s += sprintf(s, "\t\t\t\tIsAutomaticMigrationSupported\n");
	if (mask & (1 << 6))
		s += sprintf(s, "\t\t\t\tIsSLMappingSupported\n");
	if (mask & (1 << 7))
		s += sprintf(s, "\t\t\t\tIsMKeyNVRAM\n");
	if (mask & (1 << 8))
		s += sprintf(s, "\t\t\t\tIsPKeyNVRAM\n");
	if (mask & (1 << 9))
		s += sprintf(s, "\t\t\t\tIsLedInfoSupported\n");
	if (mask & (1 << 10))
		s += sprintf(s, "\t\t\t\tIsSMdisabled\n");
	if (mask & (1 << 11))
		s += sprintf(s, "\t\t\t\tIsSystemImageGUIDsupported\n");
	if (mask & (1 << 12))
		s += sprintf(s,
			     "\t\t\t\tIsPkeySwitchExternalPortTrapSupported\n");
	if (mask & (1 << 14))
		s += sprintf(s, "\t\t\t\tIsExtendedSpeedsSupported\n");
	if (mask & (1 << 15))
		s += sprintf(s, "\t\t\t\tIsCapabilityMask2Supported\n");
	if (mask & (1 << 16))
		s += sprintf(s, "\t\t\t\tIsCommunicatonManagementSupported\n");
	if (mask & (1 << 17))
		s += sprintf(s, "\t\t\t\tIsSNMPTunnelingSupported\n");
	if (mask & (1 << 18))
		s += sprintf(s, "\t\t\t\tIsReinitSupported\n");
	if (mask & (1 << 19))
		s += sprintf(s, "\t\t\t\tIsDeviceManagementSupported\n");
	if (mask & (1 << 20))
		s += sprintf(s, "\t\t\t\tIsVendorClassSupported\n");
	if (mask & (1 << 21))
		s += sprintf(s, "\t\t\t\tIsDRNoticeSupported\n");
	if (mask & (1 << 22))
		s += sprintf(s, "\t\t\t\tIsCapabilityMaskNoticeSupported\n");
	if (mask & (1 << 23))
		s += sprintf(s, "\t\t\t\tIsBootManagementSupported\n");
	if (mask & (1 << 24))
		s += sprintf(s, "\t\t\t\tIsLinkRoundTripLatencySupported\n");
	if (mask & (1 << 25))
		s += sprintf(s, "\t\t\t\tIsClientRegistrationSupported\n");
	if (mask & (1 << 26))
		s += sprintf(s, "\t\t\t\tIsOtherLocalChangesNoticeSupported\n");
	if (mask & (1 << 27))
		s += sprintf(s,
			     "\t\t\t\tIsLinkSpeedWidthPairsTableSupported\n");
	if (mask & (1 << 28))
		s += sprintf(s, "\t\t\t\tIsVendorSpecificMadsTableSupported\n");
	if (mask & (1 << 29))
		s += sprintf(s, "\t\t\t\tIsMcastPkeyTrapSuppressionSupported\n");
	if (mask & (1 << 30))
		s += sprintf(s, "\t\t\t\tIsMulticastFDBTopSupported\n");
	if (mask & (1 << 31))
		s += sprintf(s, "\t\t\t\tIsHierarchyInfoSupported\n");

	if (s != buf)
		*(--s) = 0;
}

void mad_dump_portcapmask2(char *buf, int bufsz, void *val, int valsz)
{
	int mask = *(int *)val;
	char *s = buf;

	s += sprintf(s, "0x%x\n", mask);
	if (mask & (1 << 0))
		s += sprintf(s, "\t\t\t\tIsSetNodeDescriptionSupported\n");
	if (mask & (1 << 1))
		s += sprintf(s, "\t\t\t\tIsPortInfoExtendedSupported\n");
	if (mask & (1 << 2))
		s += sprintf(s, "\t\t\t\tIsVirtualizationSupported\n");
	if (mask & (1 << 3))
		s += sprintf(s, "\t\t\t\tIsSwitchPortStateTableSupported\n");
	if (mask & (1 << 4))
		s += sprintf(s, "\t\t\t\tIsLinkWidth2xSupported\n");

	if (s != buf)
		*(--s) = 0;
}

void mad_dump_bitfield(char *buf, int bufsz, void *val, int valsz)
{
	snprintf(buf, bufsz, "0x%x", *(uint32_t *) val);
}

void mad_dump_array(char *buf, int bufsz, void *val, int valsz)
{
	uint8_t *p = val, *e;
	char *s = buf;

	if (bufsz < valsz * 2)
		valsz = bufsz / 2;

	for (p = val, e = p + valsz; p < e; p++, s += 2)
		sprintf(s, "%02x", *p);
}

void mad_dump_string(char *buf, int bufsz, void *val, int valsz)
{
	if (bufsz < valsz)
		valsz = bufsz;

	snprintf(buf, valsz, "'%s'", (char *)val);
}

void mad_dump_node_type(char *buf, int bufsz, void *val, int valsz)
{
	int nodetype = *(int *)val;

	switch (nodetype) {
	case 1:
		snprintf(buf, bufsz, "Channel Adapter");
		break;
	case 2:
		snprintf(buf, bufsz, "Switch");
		break;
	case 3:
		snprintf(buf, bufsz, "Router");
		break;
	default:
		snprintf(buf, bufsz, "?(%d)?", nodetype);
		break;
	}
}

#define IB_MAX_NUM_VLS 16
#define IB_MAX_NUM_VLS_TO_U8 ((IB_MAX_NUM_VLS)/2)

typedef struct _ib_slvl_table {
	uint8_t vl_by_sl_num[IB_MAX_NUM_VLS_TO_U8];
} ib_slvl_table_t;

static inline void ib_slvl_get_i(ib_slvl_table_t * tbl, int i, uint8_t * vl)
{
	*vl = (tbl->vl_by_sl_num[i >> 1] >> ((!(i & 1)) << 2)) & 0xf;
}

#define IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK 32

typedef struct _ib_vl_arb_table {
	struct {
		uint8_t res_vl;
		uint8_t weight;
	} vl_entry[IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK];
} ib_vl_arb_table_t;

static inline void ib_vl_arb_get_vl(uint8_t res_vl, uint8_t * const vl)
{
	*vl = res_vl & 0x0F;
}

void mad_dump_sltovl(char *buf, int bufsz, void *val, int valsz)
{
	ib_slvl_table_t *p_slvl_tbl = val;
	uint8_t vl;
	int i, n = 0;
	n = snprintf(buf, bufsz, "|");
	for (i = 0; i < 16; i++) {
		ib_slvl_get_i(p_slvl_tbl, i, &vl);
		n += snprintf(buf + n, bufsz - n, "%2u|", vl);
		if (n >= bufsz)
			break;
	}
	snprintf(buf + n, bufsz - n, "\n");
}

void mad_dump_vlarbitration(char *buf, int bufsz, void *val, int num)
{
	ib_vl_arb_table_t *p_vla_tbl = val;
	int i, n;
	uint8_t vl;

	num /= sizeof(p_vla_tbl->vl_entry[0]);

	n = snprintf(buf, bufsz, "\nVL    : |");
	if (n >= bufsz)
		return;
	for (i = 0; i < num; i++) {
		ib_vl_arb_get_vl(p_vla_tbl->vl_entry[i].res_vl, &vl);
		n += snprintf(buf + n, bufsz - n, "0x%-2X|", vl);
		if (n >= bufsz)
			return;
	}

	n += snprintf(buf + n, bufsz - n, "\nWEIGHT: |");
	if (n >= bufsz)
		return;
	for (i = 0; i < num; i++) {
		n += snprintf(buf + n, bufsz - n, "0x%-2X|",
			      p_vla_tbl->vl_entry[i].weight);
		if (n >= bufsz)
			return;
	}

	snprintf(buf + n, bufsz - n, "\n");
}

static int _dump_fields(char *buf, int bufsz, void *data, int start, int end)
{
	char val[64];
	char *s = buf;
	int n, field;

	for (field = start; field < end && bufsz > 0; field++) {
		mad_decode_field(data, field, val);
		if (!mad_dump_field(field, s, bufsz-1, val))
			return -1;
		n = strlen(s);
		s += n;
		*s++ = '\n';
		*s = 0;
		n++;
		bufsz -= n;
	}

	return (int)(s - buf);
}

void mad_dump_fields(char *buf, int bufsz, void *val, int valsz, int start,
		     int end)
{
	_dump_fields(buf, bufsz, val, start, end);
}

void mad_dump_nodedesc(char *buf, int bufsz, void *val, int valsz)
{
	strncpy(buf, val, bufsz);

	if (valsz < bufsz)
		buf[valsz] = 0;
}

void mad_dump_nodeinfo(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_NODE_FIRST_F, IB_NODE_LAST_F);
}

void mad_dump_portinfo(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PORT_FIRST_F, IB_PORT_LAST_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val,
		     IB_PORT_CAPMASK2_F, IB_PORT_LINK_SPEED_EXT_LAST_F);
}

void mad_dump_portstates(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PORT_STATE_F, IB_PORT_LINK_DOWN_DEF_F);
}

void mad_dump_switchinfo(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_SW_FIRST_F, IB_SW_LAST_F);
}

void mad_dump_perfcounters(char *buf, int bufsz, void *val, int valsz)
{
	int cnt, cnt2;

	cnt = _dump_fields(buf, bufsz, val,
			   IB_PC_FIRST_F, IB_PC_VL15_DROPPED_F);
	if (cnt < 0)
		return;

	cnt2 = _dump_fields(buf + cnt, bufsz - cnt, val,
			    IB_PC_QP1_DROP_F, IB_PC_QP1_DROP_F + 1);
	if (cnt2 < 0)
		return;

	_dump_fields(buf + cnt + cnt2, bufsz - cnt - cnt2, val,
		     IB_PC_VL15_DROPPED_F, IB_PC_LAST_F);
}

void mad_dump_perfcounters_ext(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_FIRST_F, IB_PC_EXT_LAST_F);
	if (cnt < 0)
		return;

	 _dump_fields(buf + cnt, bufsz - cnt, val,
		      IB_PC_EXT_COUNTER_SELECT2_F, IB_PC_EXT_ERR_LAST_F);
}

void mad_dump_perfcounters_xmt_sl(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_XMT_DATA_SL_FIRST_F,
		     IB_PC_XMT_DATA_SL_LAST_F);
}

void mad_dump_perfcounters_rcv_sl(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_RCV_DATA_SL_FIRST_F,
		     IB_PC_RCV_DATA_SL_LAST_F);
}

void mad_dump_perfcounters_xmt_disc(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_XMT_INACT_DISC_F,
		     IB_PC_XMT_DISC_LAST_F);
}

void mad_dump_perfcounters_rcv_err(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_RCV_LOCAL_PHY_ERR_F,
		     IB_PC_RCV_ERR_LAST_F);
}

void mad_dump_portsamples_control(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PSC_OPCODE_F, IB_PSC_LAST_F);
}

void mad_dump_port_ext_speeds_counters_rsfec_active(char *buf, int bufsz,
						    void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PESC_RSFEC_PORT_SELECT_F,
		     IB_PESC_RSFEC_LAST_F);
}

void mad_dump_port_ext_speeds_counters(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PESC_PORT_SELECT_F, IB_PESC_LAST_F);
}

void mad_dump_perfcounters_port_op_rcv_counters(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_OP_RCV_COUNTERS_FIRST_F,
		     IB_PC_PORT_OP_RCV_COUNTERS_LAST_F);
}

void mad_dump_perfcounters_port_flow_ctl_counters(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_FLOW_CTL_COUNTERS_FIRST_F,
		     IB_PC_PORT_FLOW_CTL_COUNTERS_LAST_F);
}

void mad_dump_perfcounters_port_vl_op_packet(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_VL_OP_PACKETS_FIRST_F,
		     IB_PC_PORT_VL_OP_PACKETS_LAST_F);
}

void mad_dump_perfcounters_port_vl_op_data(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_VL_OP_DATA_FIRST_F,
		     IB_PC_PORT_VL_OP_DATA_LAST_F);
}

void mad_dump_perfcounters_port_vl_xmit_flow_ctl_update_errors(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS_FIRST_F,
		     IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS_LAST_F);
}

void mad_dump_perfcounters_port_vl_xmit_wait_counters(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_PORT_VL_XMIT_WAIT_COUNTERS_FIRST_F,
		     IB_PC_PORT_VL_XMIT_WAIT_COUNTERS_LAST_F);
}

void mad_dump_perfcounters_sw_port_vl_congestion(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_SW_PORT_VL_CONGESTION_FIRST_F,
		     IB_PC_SW_PORT_VL_CONGESTION_LAST_F);
}

void mad_dump_perfcounters_rcv_con_ctrl(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_RCV_CON_CTRL_FIRST_F,
		     IB_PC_RCV_CON_CTRL_LAST_F);
}


void mad_dump_perfcounters_sl_rcv_fecn(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_SL_RCV_FECN_FIRST_F,
		     IB_PC_SL_RCV_FECN_LAST_F);
}

void mad_dump_perfcounters_sl_rcv_becn(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_SL_RCV_BECN_FIRST_F,
		     IB_PC_SL_RCV_BECN_LAST_F);
}

void mad_dump_perfcounters_xmit_con_ctrl(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_XMIT_CON_CTRL_FIRST_F,
		     IB_PC_XMIT_CON_CTRL_LAST_F);
}

void mad_dump_perfcounters_vl_xmit_time_cong(char *buf, int bufsz, void *val, int valsz)
{
	int cnt;

	cnt = _dump_fields(buf, bufsz, val, IB_PC_EXT_PORT_SELECT_F,
			   IB_PC_EXT_XMT_BYTES_F);
	if (cnt < 0)
		return;

	_dump_fields(buf + cnt, bufsz - cnt, val, IB_PC_VL_XMIT_TIME_CONG_FIRST_F,
		     IB_PC_VL_XMIT_TIME_CONG_LAST_F);
}

void mad_dump_mlnx_ext_port_info(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_MLNX_EXT_PORT_STATE_CHG_ENABLE_F,
		     IB_MLNX_EXT_PORT_LAST_F);
}

void mad_dump_portsamples_result(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PSR_TAG_F, IB_PSR_LAST_F);
}

void mad_dump_cc_congestioninfo(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_INFO_FIRST_F,
		     IB_CC_CONGESTION_INFO_LAST_F);
}

void mad_dump_cc_congestionkeyinfo(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_KEY_INFO_FIRST_F,
		     IB_CC_CONGESTION_KEY_INFO_LAST_F);
}

void mad_dump_cc_congestionlog(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_LOG_FIRST_F,
		     IB_CC_CONGESTION_LOG_LAST_F);
}

void mad_dump_cc_congestionlogswitch(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_LOG_SWITCH_FIRST_F,
		     IB_CC_CONGESTION_LOG_SWITCH_LAST_F);
}

void mad_dump_cc_congestionlogentryswitch(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_LOG_ENTRY_SWITCH_FIRST_F,
		     IB_CC_CONGESTION_LOG_ENTRY_SWITCH_LAST_F);
}

void mad_dump_cc_congestionlogca(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_LOG_CA_FIRST_F,
		     IB_CC_CONGESTION_LOG_CA_LAST_F);
}

void mad_dump_cc_congestionlogentryca(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_LOG_ENTRY_CA_FIRST_F,
		     IB_CC_CONGESTION_LOG_ENTRY_CA_LAST_F);
}

void mad_dump_cc_switchcongestionsetting(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_SWITCH_CONGESTION_SETTING_FIRST_F,
		     IB_CC_SWITCH_CONGESTION_SETTING_LAST_F);
}

void mad_dump_cc_switchportcongestionsettingelement(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_FIRST_F,
		     IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_LAST_F);
}

void mad_dump_cc_cacongestionsetting(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CA_CONGESTION_SETTING_FIRST_F,
		     IB_CC_CA_CONGESTION_SETTING_LAST_F);
}

void mad_dump_cc_cacongestionentry(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CA_CONGESTION_ENTRY_FIRST_F,
		     IB_CC_CA_CONGESTION_ENTRY_LAST_F);
}

void mad_dump_cc_congestioncontroltable(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_CONTROL_TABLE_FIRST_F,
		     IB_CC_CONGESTION_CONTROL_TABLE_LAST_F);
}

void mad_dump_cc_congestioncontroltableentry(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_CONGESTION_CONTROL_TABLE_ENTRY_FIRST_F,
		     IB_CC_CONGESTION_CONTROL_TABLE_ENTRY_LAST_F);
}

void mad_dump_cc_timestamp(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_CC_TIMESTAMP_FIRST_F,
		     IB_CC_TIMESTAMP_LAST_F);
}

void mad_dump_classportinfo(char *buf, int bufsz, void *val, int valsz)
{
	/* no FIRST_F and LAST_F for CPI field enums, must do a hack */
	_dump_fields(buf, bufsz, val, IB_CPI_BASEVER_F, IB_CPI_TRAP_QKEY_F + 1);
}

void mad_dump_portmirror_route(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PMR_FIRST_F, IB_PMR_LAST_F);
}

void mad_dump_portmirror_filter(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PMF_FIRST_F, IB_PMF_LAST_F);
}

void mad_dump_portmirror_ports(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PMP_FIRST_F, IB_PMP_LAST_F);
}

void mad_dump_portinfo_ext(char *buf, int bufsz, void *val, int valsz)
{
	_dump_fields(buf, bufsz, val, IB_PORT_EXT_CAPMASK_F,
		     IB_PORT_EXT_LAST_F);
}

void xdump(FILE * file, char *msg, void *p, int size)
{
#define HEX(x)  ((x) < 10 ? '0' + (x) : 'a' + ((x) -10))
	uint8_t *cp = p;
	int i;

	if (msg)
		fputs(msg, file);

	for (i = 0; i < size;) {
		fputc(HEX(*cp >> 4), file);
		fputc(HEX(*cp & 0xf), file);
		if (++i >= size)
			break;
		fputc(HEX(cp[1] >> 4), file);
		fputc(HEX(cp[1] & 0xf), file);
		if ((++i) % 16)
			fputc(' ', file);
		else
			fputc('\n', file);
		cp += 2;
	}
	if (i % 16)
		fputc('\n', file);
}
