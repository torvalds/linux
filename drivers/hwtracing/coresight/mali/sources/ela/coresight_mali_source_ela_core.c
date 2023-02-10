// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/of_platform.h>
#include <coresight-priv.h>
#include "sources/coresight_mali_sources.h"
#include "coresight-ela600.h"

/* Linux Coresight framework does not support multiple sources enabled
 * at the same time.
 *
 * To avoid Kernel instability, all Mali Coresight sources use the
 * same trace ID value as the mandatory ETM one.
 */
#define CS_MALI_TRACE_ID 0x00000010

#define CS_ELA_BASE_ADDR 0xE0043000
#define CS_GPU_COMMAND_ADDR 0x40003030
#define CS_GPU_COMMAND_TRACE_CONTROL_EN 0x000001DC
#define CS_ELA_MAX_SIGNAL_GROUPS 12U
#define CS_SG_NAME_MAX_LEN 10U
#define CS_ELA_NR_SIG_REGS 8U

#define NELEMS(s) (sizeof(s) / sizeof((s)[0]))

#define CS_ELA_SIGREGS_ATTR_RW(_a, _b)                                                             \
	static ssize_t _a##_show(struct device *dev, struct device_attribute *attr,                \
				 char *const buf)                                                  \
	{                                                                                          \
		return sprintf_regs(buf, CS_ELA_##_b##_0, CS_ELA_##_b##_7);                        \
	}                                                                                          \
	static ssize_t _a##_store(struct device *dev, struct device_attribute *attr,               \
				  const char *buf, size_t count)                                   \
	{                                                                                          \
		return verify_store_8_regs(dev, buf, count, CS_ELA_##_b##_0);                      \
	}                                                                                          \
	static DEVICE_ATTR_RW(_a)

enum cs_ela_dynamic_regs {
	CS_ELA_TIMECTRL,
	CS_ELA_TSSR,

	CS_ELA_SIGSEL0,
	CS_ELA_COMPCTRL0,
	CS_ELA_ALTCOMPCTRL0,
	CS_ELA_TWBSEL0,
	CS_ELA_QUALMASK0,
	CS_ELA_QUALCOMP0,
	CS_ELA_SIGMASK0_0,
	CS_ELA_SIGMASK0_1,
	CS_ELA_SIGMASK0_2,
	CS_ELA_SIGMASK0_3,
	CS_ELA_SIGMASK0_4,
	CS_ELA_SIGMASK0_5,
	CS_ELA_SIGMASK0_6,
	CS_ELA_SIGMASK0_7,
	CS_ELA_SIGCOMP0_0,
	CS_ELA_SIGCOMP0_1,
	CS_ELA_SIGCOMP0_2,
	CS_ELA_SIGCOMP0_3,
	CS_ELA_SIGCOMP0_4,
	CS_ELA_SIGCOMP0_5,
	CS_ELA_SIGCOMP0_6,
	CS_ELA_SIGCOMP0_7,

	CS_ELA_SIGSEL4,
	CS_ELA_NEXTSTATE4,
	CS_ELA_ACTION4,
	CS_ELA_ALTNEXTSTATE4,
	CS_ELA_COMPCTRL4,
	CS_ELA_TWBSEL4,
	CS_ELA_SIGMASK4_0,
	CS_ELA_SIGMASK4_1,
	CS_ELA_SIGMASK4_2,
	CS_ELA_SIGMASK4_3,
	CS_ELA_SIGMASK4_4,
	CS_ELA_SIGMASK4_5,
	CS_ELA_SIGMASK4_6,
	CS_ELA_SIGMASK4_7,
	CS_ELA_SIGCOMP4_0,
	CS_ELA_SIGCOMP4_1,
	CS_ELA_SIGCOMP4_2,
	CS_ELA_SIGCOMP4_3,
	CS_ELA_SIGCOMP4_4,
	CS_ELA_SIGCOMP4_5,
	CS_ELA_SIGCOMP4_6,
	CS_ELA_SIGCOMP4_7,

	CS_ELA_NR_DYN_REGS
};

enum cs_ela_tracemodes {
	CS_ELA_TRACEMODE_NONE,
	CS_ELA_TRACEMODE_JCN,
	CS_ELA_TRACEMODE_CEU_EXEC,
	CS_ELA_TRACEMODE_CEU_CMDS,
	CS_ELA_TRACEMODE_MCU_AHBP,
	CS_ELA_TRACEMODE_HOST_AXI,
	CS_ELA_NR_TRACEMODE
};

enum cs_ela_signal_types {
	CS_ELA_SIGTYPE_JCN_REQ,
	CS_ELA_SIGTYPE_JCN_RES,
	CS_ELA_SIGTYPE_CEU_EXEC,
	CS_ELA_SIGTYPE_CEU_CMDS,
	CS_ELA_SIGTYPE_MCU_AHBP,
	CS_ELA_SIGTYPE_HOST_AXI,
	CS_ELA_NR_SIGTYPE,
};

struct cs_ela_state {
	enum cs_ela_tracemodes tracemode;
	u32 supported_tracemodes;
	int enabled;
	u32 signal_types[CS_ELA_NR_SIGTYPE];
	u32 regs[CS_ELA_NR_DYN_REGS];
};

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
static char *type_name = "mali-source-ela";
#endif

static struct cs_ela_state ela_state = { 0 };

/* Setup ELA sysfs attributes */
static char *tracemode_names[] = {
	[CS_ELA_TRACEMODE_NONE] = "NONE",	  [CS_ELA_TRACEMODE_JCN] = "JCN",
	[CS_ELA_TRACEMODE_CEU_EXEC] = "CEU_EXEC", [CS_ELA_TRACEMODE_CEU_CMDS] = "CEU_CMDS",
	[CS_ELA_TRACEMODE_MCU_AHBP] = "MCU_AHBP", [CS_ELA_TRACEMODE_HOST_AXI] = "HOST_AXI",
};

static char *signal_type_names[] = {
	[CS_ELA_SIGTYPE_JCN_REQ] = "jcn-request",    [CS_ELA_SIGTYPE_JCN_RES] = "jcn-response",
	[CS_ELA_SIGTYPE_CEU_EXEC] = "ceu-execution", [CS_ELA_SIGTYPE_CEU_CMDS] = "ceu-commands",
	[CS_ELA_SIGTYPE_MCU_AHBP] = "mcu-ahbp",	     [CS_ELA_SIGTYPE_HOST_AXI] = "host-axi",
};

static int signal_type_tracemode_map[] = {
	[CS_ELA_SIGTYPE_JCN_REQ] = CS_ELA_TRACEMODE_JCN,
	[CS_ELA_SIGTYPE_JCN_RES] = CS_ELA_TRACEMODE_JCN,
	[CS_ELA_SIGTYPE_CEU_EXEC] = CS_ELA_TRACEMODE_CEU_EXEC,
	[CS_ELA_SIGTYPE_CEU_CMDS] = CS_ELA_TRACEMODE_CEU_CMDS,
	[CS_ELA_SIGTYPE_MCU_AHBP] = CS_ELA_TRACEMODE_MCU_AHBP,
	[CS_ELA_SIGTYPE_HOST_AXI] = CS_ELA_TRACEMODE_HOST_AXI,
};

static void setup_tracemode_registers(int tracemode)
{
	switch (tracemode) {
	case CS_ELA_TRACEMODE_NONE:
		/* Perform full reset of all dynamic registers */
		memset(ela_state.regs, 0x00000000, sizeof(u32) * CS_ELA_NR_DYN_REGS);

		ela_state.tracemode = CS_ELA_TRACEMODE_NONE;
		break;
	case CS_ELA_TRACEMODE_JCN:

		if (ela_state.signal_types[CS_ELA_SIGTYPE_JCN_REQ] ==
		    ela_state.signal_types[CS_ELA_SIGTYPE_JCN_RES]) {
			ela_state.regs[CS_ELA_TSSR] = 0x00000000;

			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_JCN_REQ];

			ela_state.regs[CS_ELA_COMPCTRL0] = 0x00000010;
			ela_state.regs[CS_ELA_ALTCOMPCTRL0] = 0x00001000;
			ela_state.regs[CS_ELA_TWBSEL0] = 0x0000FFFF;
			ela_state.regs[CS_ELA_QUALMASK0] = 0x00000000;
			ela_state.regs[CS_ELA_QUALCOMP0] = 0x00000000;

			memset(&ela_state.regs[CS_ELA_SIGMASK0_0], 0x00000000,
			       sizeof(u32) * (CS_ELA_SIGCOMP0_7 - CS_ELA_SIGMASK0_0 + 1));
			ela_state.regs[CS_ELA_SIGMASK0_1] = 0x80000000;
			ela_state.regs[CS_ELA_SIGMASK0_3] = 0x80000000;
			ela_state.regs[CS_ELA_SIGCOMP0_1] = 0x80000000;
			ela_state.regs[CS_ELA_SIGCOMP0_3] = 0x80000000;

			memset(&ela_state.regs[CS_ELA_SIGSEL4], 0x00000000,
			       sizeof(u32) * (CS_ELA_SIGCOMP4_7 - CS_ELA_SIGSEL4 + 1));

			ela_state.regs[CS_ELA_COMPCTRL4] = 0x11111111;

		} else {
			ela_state.regs[CS_ELA_TSSR] = 0x00000010;

			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_JCN_REQ];

			ela_state.regs[CS_ELA_COMPCTRL0] = 0x00000100;
			ela_state.regs[CS_ELA_ALTCOMPCTRL0] = 0x11111111;
			ela_state.regs[CS_ELA_TWBSEL0] = 0x00000FFF;
			ela_state.regs[CS_ELA_QUALMASK0] = 0x00000000;
			ela_state.regs[CS_ELA_QUALCOMP0] = 0x00000000;

			memset(&ela_state.regs[CS_ELA_SIGMASK0_0], 0x00000000,
			       sizeof(u32) * (CS_ELA_SIGCOMP0_7 - CS_ELA_SIGMASK0_0 + 1));
			ela_state.regs[CS_ELA_SIGMASK0_2] |= 0x80000000;
			ela_state.regs[CS_ELA_SIGCOMP0_2] |= 0x80000000;

			ela_state.regs[CS_ELA_SIGSEL4] =
				ela_state.signal_types[CS_ELA_SIGTYPE_JCN_RES];
			ela_state.regs[CS_ELA_NEXTSTATE4] = 0x00000010;
			ela_state.regs[CS_ELA_ACTION4] = 0x00000008;
			ela_state.regs[CS_ELA_ALTNEXTSTATE4] = 0x00000001;
			ela_state.regs[CS_ELA_COMPCTRL4] = 0x00000100;
			ela_state.regs[CS_ELA_TWBSEL4] = 0x00000FFF;

			memset(&ela_state.regs[CS_ELA_SIGMASK4_0], 0x00000000,
			       sizeof(u32) * (CS_ELA_SIGCOMP4_7 - CS_ELA_SIGMASK4_0 + 1));
			ela_state.regs[CS_ELA_SIGMASK4_2] |= 0x80000000;
			ela_state.regs[CS_ELA_SIGCOMP4_2] |= 0x80000000;
		}

		break;
	case CS_ELA_TRACEMODE_CEU_EXEC:
	case CS_ELA_TRACEMODE_CEU_CMDS:
		ela_state.regs[CS_ELA_TSSR] = 0x00000000;

		if (tracemode == CS_ELA_TRACEMODE_CEU_EXEC) {
			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_CEU_EXEC];
			ela_state.regs[CS_ELA_ALTCOMPCTRL0] = 0x00001000;
		} else if (tracemode == CS_ELA_TRACEMODE_CEU_CMDS) {
			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_CEU_CMDS];
			ela_state.regs[CS_ELA_ALTCOMPCTRL0] = 0x11111111;
		}

		ela_state.regs[CS_ELA_COMPCTRL0] = 0x00000001;
		ela_state.regs[CS_ELA_TWBSEL0] = 0x0000FFFF;
		ela_state.regs[CS_ELA_QUALMASK0] = 0x0000000F;
		ela_state.regs[CS_ELA_QUALCOMP0] = 0x0000000F;

		memset(&ela_state.regs[CS_ELA_SIGMASK0_0], 0x00000000,
		       sizeof(u32) * (CS_ELA_SIGCOMP0_7 - CS_ELA_SIGMASK0_0 + 1));

		memset(&ela_state.regs[CS_ELA_SIGSEL4], 0x00000000,
		       sizeof(u32) * (CS_ELA_SIGCOMP4_7 - CS_ELA_SIGSEL4 + 1));

		ela_state.regs[CS_ELA_COMPCTRL4] = 0x11111111;

		break;
	case CS_ELA_TRACEMODE_MCU_AHBP:
	case CS_ELA_TRACEMODE_HOST_AXI:
		ela_state.regs[CS_ELA_TSSR] = 0x00000000;

		if (tracemode == CS_ELA_TRACEMODE_MCU_AHBP)
			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_MCU_AHBP];
		else if (tracemode == CS_ELA_TRACEMODE_HOST_AXI)
			ela_state.regs[CS_ELA_SIGSEL0] =
				ela_state.signal_types[CS_ELA_SIGTYPE_HOST_AXI];

		ela_state.regs[CS_ELA_COMPCTRL0] = 0x00000001;
		ela_state.regs[CS_ELA_ALTCOMPCTRL0] = 0x11111111;
		ela_state.regs[CS_ELA_TWBSEL0] = 0x000000FF;
		ela_state.regs[CS_ELA_QUALMASK0] = 0x00000003;
		ela_state.regs[CS_ELA_QUALCOMP0] = 0x00000003;

		memset(&ela_state.regs[CS_ELA_SIGMASK0_0], 0x00000000,
		       sizeof(u32) * (CS_ELA_SIGCOMP0_7 - CS_ELA_SIGMASK0_0 + 1));

		memset(&ela_state.regs[CS_ELA_SIGSEL4], 0x00000000,
		       sizeof(u32) * (CS_ELA_SIGCOMP4_7 - CS_ELA_SIGSEL4 + 1));

		ela_state.regs[CS_ELA_COMPCTRL4] = 0x11111111;

		break;
	}
	ela_state.tracemode = tracemode;
}

static ssize_t select_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	ssize_t ret = 0;
	unsigned int mode;

	for (mode = CS_ELA_TRACEMODE_NONE; mode < CS_ELA_NR_TRACEMODE; mode++) {
		if (ela_state.supported_tracemodes & (1U << mode)) {
			if (ela_state.tracemode == mode)
				ret += sprintf(buf + ret, "[%s]\n", tracemode_names[mode]);
			else
				ret += sprintf(buf + ret, "%s\n", tracemode_names[mode]);
		}
	}
	return ret;
}

static ssize_t select_store(struct device *dev, struct device_attribute *attr, const char *buf,
			    size_t count)
{
	struct coresight_mali_source_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int mode = 0;

	/* Check if enabled and return error */
	if (ela_state.enabled == 1) {
		dev_err(drvdata->base.dev,
			"Config needs to be disabled before modifying registers");
		return -EINVAL;
	}

	for (mode = CS_ELA_TRACEMODE_NONE; mode < CS_ELA_NR_TRACEMODE; mode++) {
		if (sysfs_streq(tracemode_names[mode], buf) &&
		    (ela_state.supported_tracemodes & (1U << mode))) {
			setup_tracemode_registers(mode);
			return count;
		}
	}

	dev_err(drvdata->base.dev, "Invalid tracemode: %s", buf);
	return -EINVAL;
}

static DEVICE_ATTR_RW(select);

static ssize_t is_enabled_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	return sprintf(buf, "%d\n", ela_state.enabled);
}

static DEVICE_ATTR_RO(is_enabled);

static ssize_t sprintf_regs(char *const buf, int from_reg, int to_reg)
{
	ssize_t ret = 0;
	unsigned int i = 0;

	for (i = from_reg; i <= to_reg; i++)
		ret += sprintf(buf + ret, "0x%08X ", ela_state.regs[i]);

	ret += sprintf(buf + ret, "\n");
	return ret;
}

static ssize_t verify_store_8_regs(struct device *dev, const char *buf, size_t count, int from_reg)
{
	struct coresight_mali_source_drvdata *drvdata = dev_get_drvdata(dev->parent);
	u32 regs[CS_ELA_NR_SIG_REGS] = { 0 };
	int items;
	unsigned int i;

	if (ela_state.enabled == 1) {
		dev_err(drvdata->base.dev,
			"Config needs to be disabled before modifying registers");
		return -EINVAL;
	}

	items = sscanf(buf, "%x %x %x %x %x %x %x %x", &regs[0], &regs[1], &regs[2], &regs[3],
		       &regs[4], &regs[5], &regs[6], &regs[7]);
	if (items <= 0) {
		dev_err(drvdata->base.dev, "Invalid register value");
		return -EINVAL;
	}
	if (items != CS_ELA_NR_SIG_REGS) {
		dev_err(drvdata->base.dev, "Incorrect number of registers set (%d != %d)", items,
			CS_ELA_NR_SIG_REGS);
		return -EINVAL;
	}
	for (i = 0; i < CS_ELA_NR_SIG_REGS; i++)
		ela_state.regs[from_reg + i] = regs[i];

	return count;
}

CS_ELA_SIGREGS_ATTR_RW(sigmask0, SIGMASK0);
CS_ELA_SIGREGS_ATTR_RW(sigcomp0, SIGCOMP0);
CS_ELA_SIGREGS_ATTR_RW(sigmask4, SIGMASK4);
CS_ELA_SIGREGS_ATTR_RW(sigcomp4, SIGCOMP4);

static struct attribute *coresight_ela_attrs[] = {
	&dev_attr_select.attr,
	&dev_attr_is_enabled.attr,
	&dev_attr_sigmask0.attr,
	&dev_attr_sigcomp0.attr,
	&dev_attr_sigmask4.attr,
	&dev_attr_sigcomp4.attr,
	NULL,
};

static struct attribute_group coresight_ela_group = {
	.attrs = coresight_ela_attrs,
};

static const struct attribute_group *coresight_ela_groups[] = {
	&coresight_ela_group,
	NULL,
};

const struct attribute_group **coresight_mali_source_groups_get(void)
{
	return coresight_ela_groups;
}

/* Initialize ELA coresight driver */

static struct kbase_debug_coresight_csf_address_range ela_range[] = {
	{ CS_ELA_BASE_ADDR, CS_ELA_BASE_ADDR + CORESIGHT_DEVTYPE },
	{ CS_GPU_COMMAND_ADDR, CS_GPU_COMMAND_ADDR }
};

static struct kbase_debug_coresight_csf_op ela_enable_ops[] = {
	/* Clearing CTRL.RUN and the read only CTRL.TRACE_BUSY. */
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_CTRL, 0x00000000),
	/* Poll CTRL.TRACE_BUSY until it becomes low to ensure that trace has stopped. */
	POLL_OP(CS_ELA_BASE_ADDR + ELA_CTRL, ELA_CTRL_TRACE_BUSY, 0x0),
	/* 0 for now. TSEN = 1 or TSINT = 8 in future */
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_TIMECTRL, &ela_state.regs[CS_ELA_TIMECTRL]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_TSSR, &ela_state.regs[CS_ELA_TSSR]),
	/* ATID[6:0] = 4; valid range 0x1-0x6F, value must be unique and needs to be
	 * known for trace extraction
	 */
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ATBCTRL, 0x00000400),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_PTACTION, ELA_ACTION_TRACE),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_AUXCTRL, 0x00000000),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_CNTSEL, 0x00000000),

	/* Trigger State 0 */
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGSEL(0), &ela_state.regs[CS_ELA_SIGSEL0]),
	/* May need to be configurable in future. */
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_TRIGCTRL(0), 0x00000000),

	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_NEXTSTATE(0), 0x00000001),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ACTION(0), ELA_ACTION_TRACE),

	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTNEXTSTATE(0), 0x00000001),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTACTION(0), ELA_ACTION_TRACE),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_COMPCTRL(0), &ela_state.regs[CS_ELA_COMPCTRL0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_ALTCOMPCTRL(0), &ela_state.regs[CS_ELA_ALTCOMPCTRL0]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_COUNTCOMP(0), 0x00000000),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_TWBSEL(0), &ela_state.regs[CS_ELA_TWBSEL0]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_EXTMASK(0), 0x00000000),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_EXTCOMP(0), 0x00000000),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_QUALMASK(0), &ela_state.regs[CS_ELA_QUALMASK0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_QUALCOMP(0), &ela_state.regs[CS_ELA_QUALCOMP0]),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 0), &ela_state.regs[CS_ELA_SIGMASK0_0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 1), &ela_state.regs[CS_ELA_SIGMASK0_1]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 2), &ela_state.regs[CS_ELA_SIGMASK0_2]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 3), &ela_state.regs[CS_ELA_SIGMASK0_3]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 4), &ela_state.regs[CS_ELA_SIGMASK0_4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 5), &ela_state.regs[CS_ELA_SIGMASK0_5]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 6), &ela_state.regs[CS_ELA_SIGMASK0_6]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(0, 7), &ela_state.regs[CS_ELA_SIGMASK0_7]),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 0), &ela_state.regs[CS_ELA_SIGCOMP0_0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 1), &ela_state.regs[CS_ELA_SIGCOMP0_1]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 2), &ela_state.regs[CS_ELA_SIGCOMP0_2]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 3), &ela_state.regs[CS_ELA_SIGCOMP0_3]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 4), &ela_state.regs[CS_ELA_SIGCOMP0_4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 5), &ela_state.regs[CS_ELA_SIGCOMP0_5]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 6), &ela_state.regs[CS_ELA_SIGCOMP0_6]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(0, 7), &ela_state.regs[CS_ELA_SIGCOMP0_7]),

	WRITE_RANGE_OP(CS_ELA_BASE_ADDR + ELA_SIGSEL(1), CS_ELA_BASE_ADDR + ELA_SIGCOMP(1, 7),
		       0x00000000),
	WRITE_RANGE_OP(CS_ELA_BASE_ADDR + ELA_SIGSEL(2), CS_ELA_BASE_ADDR + ELA_SIGCOMP(2, 7),
		       0x00000000),
	WRITE_RANGE_OP(CS_ELA_BASE_ADDR + ELA_SIGSEL(3), CS_ELA_BASE_ADDR + ELA_SIGCOMP(3, 7),
		       0x00000000),

	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_COMPCTRL(1), 0x11111111),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_COMPCTRL(2), 0x11111111),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_COMPCTRL(3), 0x11111111),

	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTCOMPCTRL(1), 0x11111111),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTCOMPCTRL(2), 0x11111111),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTCOMPCTRL(3), 0x11111111),

	/* Trigger State 4 */
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGSEL(4), &ela_state.regs[CS_ELA_SIGSEL4]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_TRIGCTRL(4), 0x00000000),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_NEXTSTATE(4), &ela_state.regs[CS_ELA_NEXTSTATE4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_ACTION(4), &ela_state.regs[CS_ELA_ACTION4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_ALTNEXTSTATE(4), &ela_state.regs[CS_ELA_ALTNEXTSTATE4]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTACTION(4), ELA_ACTION_TRACE),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_COMPCTRL(4), &ela_state.regs[CS_ELA_COMPCTRL4]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_ALTCOMPCTRL(4), 0x11111111),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_COUNTCOMP(4), 0x00000000),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_TWBSEL(4), &ela_state.regs[CS_ELA_TWBSEL4]),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_EXTMASK(4), 0x00000000),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_EXTCOMP(4), 0x00000000),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_QUALMASK(4), 0x00000000),
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_QUALCOMP(4), 0x00000000),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 0), &ela_state.regs[CS_ELA_SIGMASK4_0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 1), &ela_state.regs[CS_ELA_SIGMASK4_1]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 2), &ela_state.regs[CS_ELA_SIGMASK4_2]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 3), &ela_state.regs[CS_ELA_SIGMASK4_3]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 4), &ela_state.regs[CS_ELA_SIGMASK4_4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 5), &ela_state.regs[CS_ELA_SIGMASK4_5]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 6), &ela_state.regs[CS_ELA_SIGMASK4_6]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGMASK(4, 7), &ela_state.regs[CS_ELA_SIGMASK4_7]),

	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 0), &ela_state.regs[CS_ELA_SIGCOMP4_0]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 1), &ela_state.regs[CS_ELA_SIGCOMP4_1]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 2), &ela_state.regs[CS_ELA_SIGCOMP4_2]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 3), &ela_state.regs[CS_ELA_SIGCOMP4_3]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 4), &ela_state.regs[CS_ELA_SIGCOMP4_4]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 5), &ela_state.regs[CS_ELA_SIGCOMP4_5]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 6), &ela_state.regs[CS_ELA_SIGCOMP4_6]),
	WRITE_PTR_OP(CS_ELA_BASE_ADDR + ELA_SIGCOMP(4, 7), &ela_state.regs[CS_ELA_SIGCOMP4_7]),

	WRITE_IMM_OP(CS_GPU_COMMAND_ADDR, CS_GPU_COMMAND_TRACE_CONTROL_EN),

	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_CTRL, ELA_CTRL_RUN),

	BIT_OR_OP(&ela_state.enabled, 0x1),
};

static struct kbase_debug_coresight_csf_op ela_disable_ops[] = {
	WRITE_IMM_OP(CS_ELA_BASE_ADDR + ELA_CTRL, 0x00000000),
	/* Poll CTRL.TRACE_BUSY until it becomes low to ensure that trace has stopped. */
	POLL_OP(CS_ELA_BASE_ADDR + ELA_CTRL, ELA_CTRL_TRACE_BUSY, 0x0),

	BIT_AND_OP(&ela_state.enabled, 0x0),
};

static int parse_signal_groups(struct coresight_mali_source_drvdata *drvdata)
{
	struct device_node *signal_groups = NULL;
	unsigned int siggrp_idx;

	if (drvdata->base.dev->of_node)
		signal_groups = of_get_child_by_name(drvdata->base.dev->of_node, "signal-groups");

	if (!signal_groups) {
		dev_err(drvdata->base.dev, "Failed to find signal groups OF node");
		return -EINVAL;
	}

	for (siggrp_idx = 0; siggrp_idx < CS_ELA_MAX_SIGNAL_GROUPS; siggrp_idx++) {
		char buf[CS_SG_NAME_MAX_LEN];
		ssize_t res;
		const char *name;
		struct property *prop;

		res = snprintf(buf, CS_SG_NAME_MAX_LEN, "sg%d", siggrp_idx);
		if (res <= 0) {
			dev_err(drvdata->base.dev,
				"Signal group name %d snprintf failed unexpectedly", siggrp_idx);
			return -EINVAL;
		}

		of_property_for_each_string(signal_groups, buf, prop, name) {
			int sig_type;

			for (sig_type = 0; sig_type < CS_ELA_NR_SIGTYPE; sig_type++) {
				if (!strncmp(signal_type_names[sig_type], name,
					     strlen(signal_type_names[sig_type]))) {
					ela_state.signal_types[sig_type] = (1U << siggrp_idx);
					ela_state.supported_tracemodes |=
						(1U << signal_type_tracemode_map[sig_type]);
				}
			}
		}
	}

	/* Add TRACEMODE_NONE as supported to allow printing */
	ela_state.supported_tracemodes |= (1U << CS_ELA_TRACEMODE_NONE);

	return 0;
}

int coresight_mali_sources_init_drvdata(struct coresight_mali_source_drvdata *drvdata)
{
	int res = 0;

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	drvdata->type_name = type_name;
#endif

	drvdata->base.kbase_client = kbase_debug_coresight_csf_register(
		drvdata->base.gpu_dev, ela_range, NELEMS(ela_range));
	if (drvdata->base.kbase_client == NULL) {
		dev_err(drvdata->base.dev, "Registration with full range failed unexpectedly");
		return -EINVAL;
	}

	drvdata->trcid = CS_MALI_TRACE_ID;

	drvdata->base.enable_seq.ops = ela_enable_ops;
	drvdata->base.enable_seq.nr_ops = NELEMS(ela_enable_ops);

	drvdata->base.disable_seq.ops = ela_disable_ops;
	drvdata->base.disable_seq.nr_ops = NELEMS(ela_disable_ops);

	drvdata->base.config = kbase_debug_coresight_csf_config_create(
		drvdata->base.kbase_client, &drvdata->base.enable_seq, &drvdata->base.disable_seq);
	if (!drvdata->base.config) {
		dev_err(drvdata->base.dev, "config create failed unexpectedly");
		return -EINVAL;
	}

	res = parse_signal_groups(drvdata);
	if (res) {
		dev_err(drvdata->base.dev, "Failed to parse signal groups");
		return res;
	}

	return 0;
}

void coresight_mali_sources_deinit_drvdata(struct coresight_mali_source_drvdata *drvdata)
{
	if (drvdata->base.config != NULL)
		kbase_debug_coresight_csf_config_free(drvdata->base.config);

	if (drvdata->base.kbase_client != NULL)
		kbase_debug_coresight_csf_unregister(drvdata->base.kbase_client);
}

static const struct of_device_id mali_source_ids[] = { { .compatible =
								 "arm,coresight-mali-source-ela" },
						       {} };

static struct platform_driver mali_sources_platform_driver = {
	.probe      = coresight_mali_sources_probe,
	.remove     = coresight_mali_sources_remove,
	.driver = {
		.name = "coresight-mali-source-ela",
		.owner = THIS_MODULE,
		.of_match_table = mali_source_ids,
		.suppress_bind_attrs    = true,
	},
};

static int __init mali_sources_init(void)
{
	return platform_driver_register(&mali_sources_platform_driver);
}

static void __exit mali_sources_exit(void)
{
	platform_driver_unregister(&mali_sources_platform_driver);
}

module_init(mali_sources_init);
module_exit(mali_sources_exit);

MODULE_AUTHOR("Arm Ltd.");
MODULE_DESCRIPTION("Arm Coresight Mali source ELA");
MODULE_LICENSE("GPL");
