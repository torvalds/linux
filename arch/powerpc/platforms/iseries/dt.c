/*
 *    Copyright (c) 2005-2006 Michael Ellerman, IBM Corporation
 *
 *    Description:
 *      This file contains all the routines to build a flattened device
 *      tree for a legacy iSeries machine.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/threads.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>	/* ETH_ALEN */

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/lppaca.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/abs_addr.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/it_exp_vpd_panel.h>
#include <asm/udbg.h>

#include "processor_vpd.h"
#include "call_hpt.h"
#include "call_pci.h"
#include "pci.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

struct blob {
	unsigned char data[PAGE_SIZE * 2];
	unsigned long next;
};

struct iseries_flat_dt {
	struct boot_param_header header;
	u64 reserve_map[2];
	struct blob dt;
	struct blob strings;
};

static struct iseries_flat_dt iseries_dt;

static void __init dt_init(struct iseries_flat_dt *dt)
{
	dt->header.off_mem_rsvmap =
		offsetof(struct iseries_flat_dt, reserve_map);
	dt->header.off_dt_struct = offsetof(struct iseries_flat_dt, dt);
	dt->header.off_dt_strings = offsetof(struct iseries_flat_dt, strings);
	dt->header.totalsize = sizeof(struct iseries_flat_dt);
	dt->header.dt_strings_size = sizeof(struct blob);

	/* There is no notion of hardware cpu id on iSeries */
	dt->header.boot_cpuid_phys = smp_processor_id();

	dt->dt.next = (unsigned long)&dt->dt.data;
	dt->strings.next = (unsigned long)&dt->strings.data;

	dt->header.magic = OF_DT_HEADER;
	dt->header.version = 0x10;
	dt->header.last_comp_version = 0x10;

	dt->reserve_map[0] = 0;
	dt->reserve_map[1] = 0;
}

static void __init dt_check_blob(struct blob *b)
{
	if (b->next >= (unsigned long)&b->next) {
		DBG("Ran out of space in flat device tree blob!\n");
		BUG();
	}
}

static void __init dt_push_u32(struct iseries_flat_dt *dt, u32 value)
{
	*((u32*)dt->dt.next) = value;
	dt->dt.next += sizeof(u32);

	dt_check_blob(&dt->dt);
}

#ifdef notyet
static void __init dt_push_u64(struct iseries_flat_dt *dt, u64 value)
{
	*((u64*)dt->dt.next) = value;
	dt->dt.next += sizeof(u64);

	dt_check_blob(&dt->dt);
}
#endif

static unsigned long __init dt_push_bytes(struct blob *blob, char *data, int len)
{
	unsigned long start = blob->next - (unsigned long)blob->data;

	memcpy((char *)blob->next, data, len);
	blob->next = _ALIGN(blob->next + len, 4);

	dt_check_blob(blob);

	return start;
}

static void __init dt_start_node(struct iseries_flat_dt *dt, char *name)
{
	dt_push_u32(dt, OF_DT_BEGIN_NODE);
	dt_push_bytes(&dt->dt, name, strlen(name) + 1);
}

#define dt_end_node(dt) dt_push_u32(dt, OF_DT_END_NODE)

static void __init dt_prop(struct iseries_flat_dt *dt, char *name,
		char *data, int len)
{
	unsigned long offset;

	dt_push_u32(dt, OF_DT_PROP);

	/* Length of the data */
	dt_push_u32(dt, len);

	/* Put the property name in the string blob. */
	offset = dt_push_bytes(&dt->strings, name, strlen(name) + 1);

	/* The offset of the properties name in the string blob. */
	dt_push_u32(dt, (u32)offset);

	/* The actual data. */
	dt_push_bytes(&dt->dt, data, len);
}

static void __init dt_prop_str(struct iseries_flat_dt *dt, char *name,
		char *data)
{
	dt_prop(dt, name, data, strlen(data) + 1); /* + 1 for NULL */
}

static void __init dt_prop_u32(struct iseries_flat_dt *dt, char *name, u32 data)
{
	dt_prop(dt, name, (char *)&data, sizeof(u32));
}

static void __init dt_prop_u64(struct iseries_flat_dt *dt, char *name, u64 data)
{
	dt_prop(dt, name, (char *)&data, sizeof(u64));
}

static void __init dt_prop_u64_list(struct iseries_flat_dt *dt, char *name,
		u64 *data, int n)
{
	dt_prop(dt, name, (char *)data, sizeof(u64) * n);
}

static void __init dt_prop_u32_list(struct iseries_flat_dt *dt, char *name,
		u32 *data, int n)
{
	dt_prop(dt, name, (char *)data, sizeof(u32) * n);
}

#ifdef notyet
static void __init dt_prop_empty(struct iseries_flat_dt *dt, char *name)
{
	dt_prop(dt, name, NULL, 0);
}
#endif

static void __init dt_cpus(struct iseries_flat_dt *dt)
{
	unsigned char buf[32];
	unsigned char *p;
	unsigned int i, index;
	struct IoHriProcessorVpd *d;
	u32 pft_size[2];

	/* yuck */
	snprintf(buf, 32, "PowerPC,%s", cur_cpu_spec->cpu_name);
	p = strchr(buf, ' ');
	if (!p) p = buf + strlen(buf);

	dt_start_node(dt, "cpus");
	dt_prop_u32(dt, "#address-cells", 1);
	dt_prop_u32(dt, "#size-cells", 0);

	pft_size[0] = 0; /* NUMA CEC cookie, 0 for non NUMA  */
	pft_size[1] = __ilog2(HvCallHpt_getHptPages() * HW_PAGE_SIZE);

	for (i = 0; i < NR_CPUS; i++) {
		if (lppaca[i].dyn_proc_status >= 2)
			continue;

		snprintf(p, 32 - (p - buf), "@%d", i);
		dt_start_node(dt, buf);

		dt_prop_str(dt, "device_type", "cpu");

		index = lppaca[i].dyn_hv_phys_proc_index;
		d = &xIoHriProcessorVpd[index];

		dt_prop_u32(dt, "i-cache-size", d->xInstCacheSize * 1024);
		dt_prop_u32(dt, "i-cache-line-size", d->xInstCacheOperandSize);

		dt_prop_u32(dt, "d-cache-size", d->xDataL1CacheSizeKB * 1024);
		dt_prop_u32(dt, "d-cache-line-size", d->xDataCacheOperandSize);

		/* magic conversions to Hz copied from old code */
		dt_prop_u32(dt, "clock-frequency",
			((1UL << 34) * 1000000) / d->xProcFreq);
		dt_prop_u32(dt, "timebase-frequency",
			((1UL << 32) * 1000000) / d->xTimeBaseFreq);

		dt_prop_u32(dt, "reg", i);

		dt_prop_u32_list(dt, "ibm,pft-size", pft_size, 2);

		dt_end_node(dt);
	}

	dt_end_node(dt);
}

static void __init dt_model(struct iseries_flat_dt *dt)
{
	char buf[16] = "IBM,";

	/* "IBM," + mfgId[2:3] + systemSerial[1:5] */
	strne2a(buf + 4, xItExtVpdPanel.mfgID + 2, 2);
	strne2a(buf + 6, xItExtVpdPanel.systemSerial + 1, 5);
	buf[11] = '\0';
	dt_prop_str(dt, "system-id", buf);

	/* "IBM," + machineType[0:4] */
	strne2a(buf + 4, xItExtVpdPanel.machineType, 4);
	buf[8] = '\0';
	dt_prop_str(dt, "model", buf);

	dt_prop_str(dt, "compatible", "IBM,iSeries");
}

static void __init dt_vdevices(struct iseries_flat_dt *dt)
{
	u32 reg = 0;
	HvLpIndexMap vlan_map;
	int i;
	char buf[32];

	dt_start_node(dt, "vdevice");
	dt_prop_str(dt, "device_type", "vdevice");
	dt_prop_str(dt, "compatible", "IBM,iSeries-vdevice");
	dt_prop_u32(dt, "#address-cells", 1);
	dt_prop_u32(dt, "#size-cells", 0);

	snprintf(buf, sizeof(buf), "vty@%08x", reg);
	dt_start_node(dt, buf);
	dt_prop_str(dt, "device_type", "serial");
	dt_prop_u32(dt, "reg", reg);
	dt_end_node(dt);
	reg++;

	snprintf(buf, sizeof(buf), "v-scsi@%08x", reg);
	dt_start_node(dt, buf);
	dt_prop_str(dt, "device_type", "vscsi");
	dt_prop_str(dt, "compatible", "IBM,v-scsi");
	dt_prop_u32(dt, "reg", reg);
	dt_end_node(dt);
	reg++;

	vlan_map = HvLpConfig_getVirtualLanIndexMap();
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; i++) {
		unsigned char mac_addr[ETH_ALEN];

		if ((vlan_map & (0x8000 >> i)) == 0)
			continue;
		snprintf(buf, 32, "l-lan@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "network");
		dt_prop_str(dt, "compatible", "IBM,iSeries-l-lan");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);

		mac_addr[0] = 0x02;
		mac_addr[1] = 0x01;
		mac_addr[2] = 0xff;
		mac_addr[3] = i;
		mac_addr[4] = 0xff;
		mac_addr[5] = HvLpConfig_getLpIndex_outline();
		dt_prop(dt, "local-mac-address", (char *)mac_addr, ETH_ALEN);
		dt_prop(dt, "mac-address", (char *)mac_addr, ETH_ALEN);
		dt_prop_u32(dt, "max-frame-size", 9000);
		dt_prop_u32(dt, "address-bits", 48);

		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALLANS;

	for (i = 0; i < HVMAXARCHITECTEDVIRTUALDISKS; i++) {
		snprintf(buf, 32, "viodasd@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "block");
		dt_prop_str(dt, "compatible", "IBM,iSeries-viodasd");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALDISKS;
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALCDROMS; i++) {
		snprintf(buf, 32, "viocd@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "block");
		dt_prop_str(dt, "compatible", "IBM,iSeries-viocd");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALCDROMS;
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALTAPES; i++) {
		snprintf(buf, 32, "viotape@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "byte");
		dt_prop_str(dt, "compatible", "IBM,iSeries-viotape");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}

	dt_end_node(dt);
}

struct pci_class_name {
	u16 code;
	char *name;
	char *type;
};

static struct pci_class_name __initdata pci_class_name[] = {
	{ PCI_CLASS_NETWORK_ETHERNET, "ethernet", "network" },
};

static struct pci_class_name * __init dt_find_pci_class_name(u16 class_code)
{
	struct pci_class_name *cp;

	for (cp = pci_class_name;
			cp < &pci_class_name[ARRAY_SIZE(pci_class_name)]; cp++)
		if (cp->code == class_code)
			return cp;
	return NULL;
}

/*
 * This assumes that the node slot is always on the primary bus!
 */
static void __init scan_bridge_slot(struct iseries_flat_dt *dt,
		HvBusNumber bus, struct HvCallPci_BridgeInfo *bridge_info)
{
	HvSubBusNumber sub_bus = bridge_info->subBusNumber;
	u16 vendor_id;
	u16 device_id;
	u32 class_id;
	int err;
	char buf[32];
	u32 reg[5];
	int id_sel = ISERIES_GET_DEVICE_FROM_SUBBUS(sub_bus);
	int function = ISERIES_GET_FUNCTION_FROM_SUBBUS(sub_bus);
	HvAgentId eads_id_sel = ISERIES_PCI_AGENTID(id_sel, function);
	u8 devfn;
	struct pci_class_name *cp;

	/*
	 * Connect all functions of any device found.
	 */
	for (id_sel = 1; id_sel <= bridge_info->maxAgents; id_sel++) {
		for (function = 0; function < 8; function++) {
			HvAgentId agent_id = ISERIES_PCI_AGENTID(id_sel,
					function);
			err = HvCallXm_connectBusUnit(bus, sub_bus,
					agent_id, 0);
			if (err) {
				if (err != 0x302)
					printk(KERN_DEBUG
						"connectBusUnit(%x, %x, %x) "
						"== %x\n",
						bus, sub_bus, agent_id, err);
				continue;
			}

			err = HvCallPci_configLoad16(bus, sub_bus, agent_id,
					PCI_VENDOR_ID, &vendor_id);
			if (err) {
				printk(KERN_DEBUG
					"ReadVendor(%x, %x, %x) == %x\n",
					bus, sub_bus, agent_id, err);
				continue;
			}
			err = HvCallPci_configLoad16(bus, sub_bus, agent_id,
					PCI_DEVICE_ID, &device_id);
			if (err) {
				printk(KERN_DEBUG
					"ReadDevice(%x, %x, %x) == %x\n",
					bus, sub_bus, agent_id, err);
				continue;
			}
			err = HvCallPci_configLoad32(bus, sub_bus, agent_id,
					PCI_CLASS_REVISION , &class_id);
			if (err) {
				printk(KERN_DEBUG
					"ReadClass(%x, %x, %x) == %x\n",
					bus, sub_bus, agent_id, err);
				continue;
			}

			devfn = PCI_DEVFN(ISERIES_ENCODE_DEVICE(eads_id_sel),
					function);
			cp = dt_find_pci_class_name(class_id >> 16);
			if (cp && cp->name)
				strncpy(buf, cp->name, sizeof(buf) - 1);
			else
				snprintf(buf, sizeof(buf), "pci%x,%x",
						vendor_id, device_id);
			buf[sizeof(buf) - 1] = '\0';
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"@%x", PCI_SLOT(devfn));
			buf[sizeof(buf) - 1] = '\0';
			if (function != 0)
				snprintf(buf + strlen(buf),
					sizeof(buf) - strlen(buf),
					",%x", function);
			dt_start_node(dt, buf);
			reg[0] = (bus << 16) | (devfn << 8);
			reg[1] = 0;
			reg[2] = 0;
			reg[3] = 0;
			reg[4] = 0;
			dt_prop_u32_list(dt, "reg", reg, 5);
			if (cp && (cp->type || cp->name))
				dt_prop_str(dt, "device_type",
					cp->type ? cp->type : cp->name);
			dt_prop_u32(dt, "vendor-id", vendor_id);
			dt_prop_u32(dt, "device-id", device_id);
			dt_prop_u32(dt, "class-code", class_id >> 8);
			dt_prop_u32(dt, "revision-id", class_id & 0xff);
			dt_prop_u32(dt, "linux,subbus", sub_bus);
			dt_prop_u32(dt, "linux,agent-id", agent_id);
			dt_prop_u32(dt, "linux,logical-slot-number",
					bridge_info->logicalSlotNumber);
			dt_end_node(dt);

		}
	}
}

static void __init scan_bridge(struct iseries_flat_dt *dt, HvBusNumber bus,
		HvSubBusNumber sub_bus, int id_sel)
{
	struct HvCallPci_BridgeInfo bridge_info;
	HvAgentId agent_id;
	int function;
	int ret;

	/* Note: hvSubBus and irq is always be 0 at this level! */
	for (function = 0; function < 8; ++function) {
		agent_id = ISERIES_PCI_AGENTID(id_sel, function);
		ret = HvCallXm_connectBusUnit(bus, sub_bus, agent_id, 0);
		if (ret != 0) {
			if (ret != 0xb)
				printk(KERN_DEBUG "connectBusUnit(%x, %x, %x) "
						"== %x\n",
						bus, sub_bus, agent_id, ret);
			continue;
		}
		printk("found device at bus %d idsel %d func %d (AgentId %x)\n",
				bus, id_sel, function, agent_id);
		ret = HvCallPci_getBusUnitInfo(bus, sub_bus, agent_id,
				iseries_hv_addr(&bridge_info),
				sizeof(struct HvCallPci_BridgeInfo));
		if (ret != 0)
			continue;
		printk("bridge info: type %x subbus %x "
			"maxAgents %x maxsubbus %x logslot %x\n",
			bridge_info.busUnitInfo.deviceType,
			bridge_info.subBusNumber,
			bridge_info.maxAgents,
			bridge_info.maxSubBusNumber,
			bridge_info.logicalSlotNumber);
		if (bridge_info.busUnitInfo.deviceType ==
				HvCallPci_BridgeDevice)
			scan_bridge_slot(dt, bus, &bridge_info);
		else
			printk("PCI: Invalid Bridge Configuration(0x%02X)",
				bridge_info.busUnitInfo.deviceType);
	}
}

static void __init scan_phb(struct iseries_flat_dt *dt, HvBusNumber bus)
{
	struct HvCallPci_DeviceInfo dev_info;
	const HvSubBusNumber sub_bus = 0;	/* EADs is always 0. */
	int err;
	int id_sel;
	const int max_agents = 8;

	/*
	 * Probe for EADs Bridges
	 */
	for (id_sel = 1; id_sel < max_agents; ++id_sel) {
		err = HvCallPci_getDeviceInfo(bus, sub_bus, id_sel,
				iseries_hv_addr(&dev_info),
				sizeof(struct HvCallPci_DeviceInfo));
		if (err) {
			if (err != 0x302)
				printk(KERN_DEBUG "getDeviceInfo(%x, %x, %x) "
						"== %x\n",
						bus, sub_bus, id_sel, err);
			continue;
		}
		if (dev_info.deviceType != HvCallPci_NodeDevice) {
			printk(KERN_DEBUG "PCI: Invalid System Configuration"
					"(0x%02X) for bus 0x%02x id 0x%02x.\n",
					dev_info.deviceType, bus, id_sel);
			continue;
		}
		scan_bridge(dt, bus, sub_bus, id_sel);
	}
}

static void __init dt_pci_devices(struct iseries_flat_dt *dt)
{
	HvBusNumber bus;
	char buf[32];
	u32 buses[2];
	int phb_num = 0;

	/* Check all possible buses. */
	for (bus = 0; bus < 256; bus++) {
		int err = HvCallXm_testBus(bus);

		if (err) {
			/*
			 * Check for Unexpected Return code, a clue that
			 * something has gone wrong.
			 */
			if (err != 0x0301)
				printk(KERN_ERR "Unexpected Return on Probe"
						"(0x%02X): 0x%04X", bus, err);
			continue;
		}
		printk("bus %d appears to exist\n", bus);
		snprintf(buf, 32, "pci@%d", phb_num);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "pci");
		dt_prop_str(dt, "compatible", "IBM,iSeries-Logical-PHB");
		dt_prop_u32(dt, "#address-cells", 3);
		dt_prop_u32(dt, "#size-cells", 2);
		buses[0] = buses[1] = bus;
		dt_prop_u32_list(dt, "bus-range", buses, 2);
		scan_phb(dt, bus);
		dt_end_node(dt);
		phb_num++;
	}
}

void * __init build_flat_dt(unsigned long phys_mem_size)
{
	u64 tmp[2];

	dt_init(&iseries_dt);

	dt_start_node(&iseries_dt, "");

	dt_prop_u32(&iseries_dt, "#address-cells", 2);
	dt_prop_u32(&iseries_dt, "#size-cells", 2);
	dt_model(&iseries_dt);

	/* /memory */
	dt_start_node(&iseries_dt, "memory@0");
	dt_prop_str(&iseries_dt, "name", "memory");
	dt_prop_str(&iseries_dt, "device_type", "memory");
	tmp[0] = 0;
	tmp[1] = phys_mem_size;
	dt_prop_u64_list(&iseries_dt, "reg", tmp, 2);
	dt_end_node(&iseries_dt);

	/* /chosen */
	dt_start_node(&iseries_dt, "chosen");
	dt_prop_str(&iseries_dt, "bootargs", cmd_line);
	dt_end_node(&iseries_dt);

	dt_cpus(&iseries_dt);

	dt_vdevices(&iseries_dt);
	dt_pci_devices(&iseries_dt);

	dt_end_node(&iseries_dt);

	dt_push_u32(&iseries_dt, OF_DT_END);

	return &iseries_dt;
}
