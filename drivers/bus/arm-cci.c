/*
 * CCI cache coherent interconnect driver
 *
 * Copyright (C) 2013 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/arm-cci.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>
#include <asm/smp_plat.h>

#define DRIVER_NAME		"CCI"

#define CCI_PORT_CTRL		0x0
#define CCI_CTRL_STATUS		0xc

#define CCI_ENABLE_SNOOP_REQ	0x1
#define CCI_ENABLE_DVM_REQ	0x2
#define CCI_ENABLE_REQ		(CCI_ENABLE_SNOOP_REQ | CCI_ENABLE_DVM_REQ)

struct cci_nb_ports {
	unsigned int nb_ace;
	unsigned int nb_ace_lite;
};

enum cci_ace_port_type {
	ACE_INVALID_PORT = 0x0,
	ACE_PORT,
	ACE_LITE_PORT,
};

struct cci_ace_port {
	void __iomem *base;
	unsigned long phys;
	enum cci_ace_port_type type;
	struct device_node *dn;
};

static struct cci_ace_port *ports;
static unsigned int nb_cci_ports;

static void __iomem *cci_ctrl_base;
static unsigned long cci_ctrl_phys;

#ifdef CONFIG_HW_PERF_EVENTS

static void __iomem *cci_pmu_base;

#define CCI400_PMCR		0x0100

#define CCI400_PMU_CYCLE_CNTR_BASE    0x0000
#define CCI400_PMU_CNTR_BASE(idx)     (CCI400_PMU_CYCLE_CNTR_BASE + (idx) * 0x1000)

#define CCI400_PMCR_CEN          0x00000001
#define CCI400_PMCR_RST          0x00000002
#define CCI400_PMCR_CCR          0x00000004
#define CCI400_PMCR_CCD          0x00000008
#define CCI400_PMCR_EX           0x00000010
#define CCI400_PMCR_DP           0x00000020
#define CCI400_PMCR_NCNT_MASK    0x0000F800
#define CCI400_PMCR_NCNT_SHIFT   11

#define CCI400_PMU_EVT_SEL       0x000
#define CCI400_PMU_CNTR          0x004
#define CCI400_PMU_CNTR_CTRL     0x008
#define CCI400_PMU_OVERFLOW      0x00C

#define CCI400_PMU_OVERFLOW_FLAG 1

enum cci400_perf_events {
	CCI400_PMU_CYCLES = 0xFF
};

#define CCI400_PMU_EVENT_MASK   0xff
#define CCI400_PMU_EVENT_SOURCE(event) ((event >> 5) & 0x7)
#define CCI400_PMU_EVENT_CODE(event) (event & 0x1f)

#define CCI400_PMU_EVENT_SOURCE_S0 0
#define CCI400_PMU_EVENT_SOURCE_S4 4
#define CCI400_PMU_EVENT_SOURCE_M0 5
#define CCI400_PMU_EVENT_SOURCE_M2 7

#define CCI400_PMU_EVENT_SLAVE_MIN 0x0
#define CCI400_PMU_EVENT_SLAVE_MAX 0x13

#define CCI400_PMU_EVENT_MASTER_MIN 0x14
#define CCI400_PMU_EVENT_MASTER_MAX 0x1A

#define CCI400_PMU_MAX_HW_EVENTS 5   /* CCI PMU has 4 counters + 1 cycle counter */

#define CCI400_PMU_CYCLE_COUNTER_IDX 0
#define CCI400_PMU_COUNTER0_IDX      1
#define CCI400_PMU_COUNTER_LAST(cci_pmu) (CCI400_PMU_CYCLE_COUNTER_IDX + cci_pmu->num_events - 1)


static struct perf_event *events[CCI400_PMU_MAX_HW_EVENTS];
static unsigned long used_mask[BITS_TO_LONGS(CCI400_PMU_MAX_HW_EVENTS)];
static struct pmu_hw_events cci_hw_events = {
	.events    = events,
	.used_mask = used_mask,
};

static int cci_pmu_validate_hw_event(u8 hw_event)
{
	u8 ev_source = CCI400_PMU_EVENT_SOURCE(hw_event);
	u8 ev_code = CCI400_PMU_EVENT_CODE(hw_event);

	if (ev_source <= CCI400_PMU_EVENT_SOURCE_S4 &&
	    ev_code <= CCI400_PMU_EVENT_SLAVE_MAX)
			return hw_event;
	else if (CCI400_PMU_EVENT_SOURCE_M0 <= ev_source &&
		   ev_source <= CCI400_PMU_EVENT_SOURCE_M2 &&
		   CCI400_PMU_EVENT_MASTER_MIN <= ev_code &&
		    ev_code <= CCI400_PMU_EVENT_MASTER_MAX)
			return hw_event;

	return -EINVAL;
}

static inline int cci_pmu_counter_is_valid(struct arm_pmu *cci_pmu, int idx)
{
	return CCI400_PMU_CYCLE_COUNTER_IDX <= idx &&
		idx <= CCI400_PMU_COUNTER_LAST(cci_pmu);
}

static inline u32 cci_pmu_read_register(int idx, unsigned int offset)
{
	return readl_relaxed(cci_pmu_base + CCI400_PMU_CNTR_BASE(idx) + offset);
}

static inline void cci_pmu_write_register(u32 value, int idx, unsigned int offset)
{
	return writel_relaxed(value, cci_pmu_base + CCI400_PMU_CNTR_BASE(idx) + offset);
}

static inline void cci_pmu_disable_counter(int idx)
{
	cci_pmu_write_register(0, idx, CCI400_PMU_CNTR_CTRL);
}

static inline void cci_pmu_enable_counter(int idx)
{
	cci_pmu_write_register(1, idx, CCI400_PMU_CNTR_CTRL);
}

static inline void cci_pmu_select_event(int idx, unsigned long event)
{
	event &= CCI400_PMU_EVENT_MASK;
	cci_pmu_write_register(event, idx, CCI400_PMU_EVT_SEL);
}

static u32 cci_pmu_get_max_counters(void)
{
	u32 n_cnts = (readl_relaxed(cci_ctrl_base + CCI400_PMCR) &
		      CCI400_PMCR_NCNT_MASK) >> CCI400_PMCR_NCNT_SHIFT;

	/* add 1 for cycle counter */
	return n_cnts + 1;
}

static struct pmu_hw_events *cci_pmu_get_hw_events(void)
{
	return &cci_hw_events;
}

static int cci_pmu_get_event_idx(struct pmu_hw_events *hw, struct perf_event *event)
{
	struct arm_pmu *cci_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hw_event = &event->hw;
	unsigned long cci_event = hw_event->config_base & CCI400_PMU_EVENT_MASK;
	int idx;

	if (cci_event == CCI400_PMU_CYCLES) {
		if (test_and_set_bit(CCI400_PMU_CYCLE_COUNTER_IDX, hw->used_mask))
			return -EAGAIN;

                return CCI400_PMU_CYCLE_COUNTER_IDX;
        }

	for (idx = CCI400_PMU_COUNTER0_IDX; idx <= CCI400_PMU_COUNTER_LAST(cci_pmu); ++idx) {
		if (!test_and_set_bit(idx, hw->used_mask))
			return idx;
	}

	/* No counters available */
	return -EAGAIN;
}

static int cci_pmu_map_event(struct perf_event *event)
{
	int mapping;
	u8 config = event->attr.config & CCI400_PMU_EVENT_MASK;

	if (event->attr.type < PERF_TYPE_MAX)
		return -ENOENT;

	/* 0xff is used to represent CCI Cycles */
	if (config == 0xff)
		mapping = config;
	else
		mapping = cci_pmu_validate_hw_event(config);

	return mapping;
}

static int cci_pmu_request_irq(struct arm_pmu *cci_pmu, irq_handler_t handler)
{
	int irq, err, i = 0;
	struct platform_device *pmu_device = cci_pmu->plat_device;

	if (unlikely(!pmu_device))
		return -ENODEV;

	/* CCI exports 6 interrupts - 1 nERRORIRQ + 5 nEVNTCNTOVERFLOW (PMU)
	   nERRORIRQ will be handled by secure firmware on TC2. So we
	   assume that all CCI interrupts listed in the linux device
	   tree are PMU interrupts.

	   The following code should then be able to handle different routing
	   of the CCI PMU interrupts.
	*/
	while ((irq = platform_get_irq(pmu_device, i)) > 0) {
		err = request_irq(irq, handler, 0, "arm-cci-pmu", cci_pmu);
		if (err) {
			dev_err(&pmu_device->dev, "unable to request IRQ%d for ARM CCI PMU counters\n",
				irq);
			return err;
		}
		i++;
	}

	return 0;
}

static irqreturn_t cci_pmu_handle_irq(int irq_num, void *dev)
{
	struct arm_pmu *cci_pmu = (struct arm_pmu *)dev;
	struct pmu_hw_events *events = cci_pmu->get_hw_events();
	struct perf_sample_data data;
	struct pt_regs *regs;
	int idx;

	regs = get_irq_regs();

	/* Iterate over counters and update the corresponding perf events.
	   This should work regardless of whether we have per-counter overflow
	   interrupt or a combined overflow interrupt. */
	for (idx = CCI400_PMU_CYCLE_COUNTER_IDX; idx <= CCI400_PMU_COUNTER_LAST(cci_pmu); idx++) {
		struct perf_event *event = events->events[idx];
		struct hw_perf_event *hw_counter;

		if (!event)
			continue;

		hw_counter = &event->hw;

		/* Did this counter overflow? */
		if (!(cci_pmu_read_register(idx, CCI400_PMU_OVERFLOW) & CCI400_PMU_OVERFLOW_FLAG))
			continue;
		cci_pmu_write_register(CCI400_PMU_OVERFLOW_FLAG, idx, CCI400_PMU_OVERFLOW);

		armpmu_event_update(event);
		perf_sample_data_init(&data, 0, hw_counter->last_period);
		if (!armpmu_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cci_pmu->disable(event);
	}

	irq_work_run();
	return IRQ_HANDLED;
}

static void cci_pmu_free_irq(struct arm_pmu *cci_pmu)
{
	int irq, i = 0;
	struct platform_device *pmu_device = cci_pmu->plat_device;

	while ((irq = platform_get_irq(pmu_device, i)) > 0) {
		free_irq(irq, cci_pmu);
		i++;
	}
}

static void cci_pmu_enable_event(struct perf_event *event)
{
	unsigned long flags;
	struct arm_pmu *cci_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cci_pmu->get_hw_events();
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;

	if (unlikely(!cci_pmu_counter_is_valid(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Configure the event to count, unless you are counting cycles */
	if (idx != CCI400_PMU_CYCLE_COUNTER_IDX)
		cci_pmu_select_event(idx, hw_counter->config_base);

	cci_pmu_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void cci_pmu_disable_event(struct perf_event *event)
{
	unsigned long flags;
	struct arm_pmu *cci_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cci_pmu->get_hw_events();
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;

	if (unlikely(!cci_pmu_counter_is_valid(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	cci_pmu_disable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void cci_pmu_start(struct arm_pmu *cci_pmu)
{
	u32 val;
	unsigned long flags;
	struct pmu_hw_events *events = cci_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Enable all the PMU counters. */
	val = readl(cci_ctrl_base + CCI400_PMCR) | CCI400_PMCR_CEN;
	writel(val, cci_ctrl_base + CCI400_PMCR);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void cci_pmu_stop(struct arm_pmu *cci_pmu)
{
	u32 val;
	unsigned long flags;
	struct pmu_hw_events *events = cci_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Disable all the PMU counters. */
	val = readl(cci_ctrl_base + CCI400_PMCR) & ~CCI400_PMCR_CEN;
	writel(val, cci_ctrl_base + CCI400_PMCR);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static u32 cci_pmu_read_counter(struct perf_event *event)
{
	struct arm_pmu *cci_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;
	u32 value;

	if (unlikely(!cci_pmu_counter_is_valid(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return 0;
	}
	value = cci_pmu_read_register(idx, CCI400_PMU_CNTR);

	return value;
}

static void cci_pmu_write_counter(struct perf_event *event, u32 value)
{
	struct arm_pmu *cci_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;

	if (unlikely(!cci_pmu_counter_is_valid(cci_pmu, idx)))
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
	else
		cci_pmu_write_register(value, idx, CCI400_PMU_CNTR);
}

static struct arm_pmu cci_pmu = {
	.name             = DRIVER_NAME,
	.max_period       = (1LLU << 32) - 1,
	.get_hw_events    = cci_pmu_get_hw_events,
	.get_event_idx    = cci_pmu_get_event_idx,
	.map_event        = cci_pmu_map_event,
	.request_irq      = cci_pmu_request_irq,
	.handle_irq       = cci_pmu_handle_irq,
	.free_irq         = cci_pmu_free_irq,
	.enable           = cci_pmu_enable_event,
	.disable          = cci_pmu_disable_event,
	.start            = cci_pmu_start,
	.stop             = cci_pmu_stop,
	.read_counter     = cci_pmu_read_counter,
	.write_counter    = cci_pmu_write_counter,
};

static int cci_pmu_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cci_pmu_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cci_pmu_base))
		return PTR_ERR(cci_pmu_base);

	cci_pmu.plat_device = pdev;
	cci_pmu.num_events = cci_pmu_get_max_counters();
	raw_spin_lock_init(&cci_hw_events.pmu_lock);
	cpumask_setall(&cci_pmu.valid_cpus);

	return armpmu_register(&cci_pmu, -1);
}

static const struct of_device_id arm_cci_pmu_matches[] = {
	{.compatible = "arm,cci-400-pmu"},
	{},
};

static struct platform_driver cci_pmu_platform_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = arm_cci_pmu_matches,
		  },
	.probe = cci_pmu_probe,
};

static int __init cci_pmu_init(void)
{
	if (platform_driver_register(&cci_pmu_platform_driver))
		WARN(1, "unable to register CCI platform driver\n");
	return 0;
}

#else

static int __init cci_pmu_init(void)
{
	return 0;
}

#endif /* CONFIG_HW_PERF_EVENTS */

struct cpu_port {
	u64 mpidr;
	u32 port;
};

/*
 * Use the port MSB as valid flag, shift can be made dynamic
 * by computing number of bits required for port indexes.
 * Code disabling CCI cpu ports runs with D-cache invalidated
 * and SCTLR bit clear so data accesses must be kept to a minimum
 * to improve performance; for now shift is left static to
 * avoid one more data access while disabling the CCI port.
 */
#define PORT_VALID_SHIFT	31
#define PORT_VALID		(0x1 << PORT_VALID_SHIFT)

static inline void init_cpu_port(struct cpu_port *port, u32 index, u64 mpidr)
{
	port->port = PORT_VALID | index;
	port->mpidr = mpidr;
}

static inline bool cpu_port_is_valid(struct cpu_port *port)
{
	return !!(port->port & PORT_VALID);
}

static inline bool cpu_port_match(struct cpu_port *port, u64 mpidr)
{
	return port->mpidr == (mpidr & MPIDR_HWID_BITMASK);
}

static struct cpu_port cpu_port[NR_CPUS];

/**
 * __cci_ace_get_port - Function to retrieve the port index connected to
 *			a cpu or device.
 *
 * @dn: device node of the device to look-up
 * @type: port type
 *
 * Return value:
 *	- CCI port index if success
 *	- -ENODEV if failure
 */
static int __cci_ace_get_port(struct device_node *dn, int type)
{
	int i;
	bool ace_match;
	struct device_node *cci_portn;

	cci_portn = of_parse_phandle(dn, "cci-control-port", 0);
	for (i = 0; i < nb_cci_ports; i++) {
		ace_match = ports[i].type == type;
		if (ace_match && cci_portn == ports[i].dn)
			return i;
	}
	return -ENODEV;
}

int cci_ace_get_port(struct device_node *dn)
{
	return __cci_ace_get_port(dn, ACE_LITE_PORT);
}
EXPORT_SYMBOL_GPL(cci_ace_get_port);

static void __init cci_ace_init_ports(void)
{
	int port, ac, cpu;
	u64 hwid;
	const u32 *cell;
	struct device_node *cpun, *cpus;

	cpus = of_find_node_by_path("/cpus");
	if (WARN(!cpus, "Missing cpus node, bailing out\n"))
		return;

	if (WARN_ON(of_property_read_u32(cpus, "#address-cells", &ac)))
		ac = of_n_addr_cells(cpus);

	/*
	 * Port index look-up speeds up the function disabling ports by CPU,
	 * since the logical to port index mapping is done once and does
	 * not change after system boot.
	 * The stashed index array is initialized for all possible CPUs
	 * at probe time.
	 */
	for_each_child_of_node(cpus, cpun) {
		if (of_node_cmp(cpun->type, "cpu"))
			continue;
		cell = of_get_property(cpun, "reg", NULL);
		if (WARN(!cell, "%s: missing reg property\n", cpun->full_name))
			continue;

		hwid = of_read_number(cell, ac);
		cpu = get_logical_index(hwid & MPIDR_HWID_BITMASK);

		if (cpu < 0 || !cpu_possible(cpu))
			continue;
		port = __cci_ace_get_port(cpun, ACE_PORT);
		if (port < 0)
			continue;

		init_cpu_port(&cpu_port[cpu], port, cpu_logical_map(cpu));
	}

	for_each_possible_cpu(cpu) {
		WARN(!cpu_port_is_valid(&cpu_port[cpu]),
			"CPU %u does not have an associated CCI port\n",
			cpu);
	}
}
/*
 * Functions to enable/disable a CCI interconnect slave port
 *
 * They are called by low-level power management code to disable slave
 * interfaces snoops and DVM broadcast.
 * Since they may execute with cache data allocation disabled and
 * after the caches have been cleaned and invalidated the functions provide
 * no explicit locking since they may run with D-cache disabled, so normal
 * cacheable kernel locks based on ldrex/strex may not work.
 * Locking has to be provided by BSP implementations to ensure proper
 * operations.
 */

/**
 * cci_port_control() - function to control a CCI port
 *
 * @port: index of the port to setup
 * @enable: if true enables the port, if false disables it
 */
static void notrace cci_port_control(unsigned int port, bool enable)
{
	void __iomem *base = ports[port].base;

	writel_relaxed(enable ? CCI_ENABLE_REQ : 0, base + CCI_PORT_CTRL);
	/*
	 * This function is called from power down procedures
	 * and must not execute any instruction that might
	 * cause the processor to be put in a quiescent state
	 * (eg wfi). Hence, cpu_relax() can not be added to this
	 * read loop to optimize power, since it might hide possibly
	 * disruptive operations.
	 */
	while (readl_relaxed(cci_ctrl_base + CCI_CTRL_STATUS) & 0x1)
			;
}

/**
 * cci_disable_port_by_cpu() - function to disable a CCI port by CPU
 *			       reference
 *
 * @mpidr: mpidr of the CPU whose CCI port should be disabled
 *
 * Disabling a CCI port for a CPU implies disabling the CCI port
 * controlling that CPU cluster. Code disabling CPU CCI ports
 * must make sure that the CPU running the code is the last active CPU
 * in the cluster ie all other CPUs are quiescent in a low power state.
 *
 * Return:
 *	0 on success
 *	-ENODEV on port look-up failure
 */
int notrace cci_disable_port_by_cpu(u64 mpidr)
{
	int cpu;
	bool is_valid;
	for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
		is_valid = cpu_port_is_valid(&cpu_port[cpu]);
		if (is_valid && cpu_port_match(&cpu_port[cpu], mpidr)) {
			cci_port_control(cpu_port[cpu].port, false);
			return 0;
		}
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(cci_disable_port_by_cpu);

/**
 * cci_enable_port_for_self() - enable a CCI port for calling CPU
 *
 * Enabling a CCI port for the calling CPU implies enabling the CCI
 * port controlling that CPU's cluster. Caller must make sure that the
 * CPU running the code is the first active CPU in the cluster and all
 * other CPUs are quiescent in a low power state  or waiting for this CPU
 * to complete the CCI initialization.
 *
 * Because this is called when the MMU is still off and with no stack,
 * the code must be position independent and ideally rely on callee
 * clobbered registers only.  To achieve this we must code this function
 * entirely in assembler.
 *
 * On success this returns with the proper CCI port enabled.  In case of
 * any failure this never returns as the inability to enable the CCI is
 * fatal and there is no possible recovery at this stage.
 */
asmlinkage void __naked cci_enable_port_for_self(void)
{
	asm volatile ("\n"

"	mrc	p15, 0, r0, c0, c0, 5	@ get MPIDR value \n"
"	and	r0, r0, #"__stringify(MPIDR_HWID_BITMASK)" \n"
"	adr	r1, 5f \n"
"	ldr	r2, [r1] \n"
"	add	r1, r1, r2		@ &cpu_port \n"
"	add	ip, r1, %[sizeof_cpu_port] \n"

	/* Loop over the cpu_port array looking for a matching MPIDR */
"1:	ldr	r2, [r1, %[offsetof_cpu_port_mpidr_lsb]] \n"
"	cmp	r2, r0 			@ compare MPIDR \n"
"	bne	2f \n"

	/* Found a match, now test port validity */
"	ldr	r3, [r1, %[offsetof_cpu_port_port]] \n"
"	tst	r3, #"__stringify(PORT_VALID)" \n"
"	bne	3f \n"

	/* no match, loop with the next cpu_port entry */
"2:	add	r1, r1, %[sizeof_struct_cpu_port] \n"
"	cmp	r1, ip			@ done? \n"
"	blo	1b \n"

	/* CCI port not found -- cheaply try to stall this CPU */
"cci_port_not_found: \n"
"	wfi \n"
"	wfe \n"
"	b	cci_port_not_found \n"

	/* Use matched port index to look up the corresponding ports entry */
"3:	bic	r3, r3, #"__stringify(PORT_VALID)" \n"
"	adr	r0, 6f \n"
"	ldmia	r0, {r1, r2} \n"
"	sub	r1, r1, r0 		@ virt - phys \n"
"	ldr	r0, [r0, r2] 		@ *(&ports) \n"
"	mov	r2, %[sizeof_struct_ace_port] \n"
"	mla	r0, r2, r3, r0		@ &ports[index] \n"
"	sub	r0, r0, r1		@ virt_to_phys() \n"

	/* Enable the CCI port */
"	ldr	r0, [r0, %[offsetof_port_phys]] \n"
"	mov	r3, %[cci_enable_req]\n"		   
"	str	r3, [r0, #"__stringify(CCI_PORT_CTRL)"] \n"

	/* poll the status reg for completion */
"	adr	r1, 7f \n"
"	ldr	r0, [r1] \n"
"	ldr	r0, [r0, r1]		@ cci_ctrl_base \n"
"4:	ldr	r1, [r0, #"__stringify(CCI_CTRL_STATUS)"] \n"
"	tst	r1, %[cci_control_status_bits] \n"			
"	bne	4b \n"

"	mov	r0, #0 \n"
"	bx	lr \n"

"	.align	2 \n"
"5:	.word	cpu_port - . \n"
"6:	.word	. \n"
"	.word	ports - 6b \n"
"7:	.word	cci_ctrl_phys - . \n"
	: :
	[sizeof_cpu_port] "i" (sizeof(cpu_port)),
	[cci_enable_req] "i" cpu_to_le32(CCI_ENABLE_REQ),
	[cci_control_status_bits] "i" cpu_to_le32(1),
#ifndef __ARMEB__
	[offsetof_cpu_port_mpidr_lsb] "i" (offsetof(struct cpu_port, mpidr)),
#else
	[offsetof_cpu_port_mpidr_lsb] "i" (offsetof(struct cpu_port, mpidr)+4),
#endif
	[offsetof_cpu_port_port] "i" (offsetof(struct cpu_port, port)),
	[sizeof_struct_cpu_port] "i" (sizeof(struct cpu_port)),
	[sizeof_struct_ace_port] "i" (sizeof(struct cci_ace_port)),
	[offsetof_port_phys] "i" (offsetof(struct cci_ace_port, phys)) );

	unreachable();
}

/**
 * __cci_control_port_by_device() - function to control a CCI port by device
 *				    reference
 *
 * @dn: device node pointer of the device whose CCI port should be
 *      controlled
 * @enable: if true enables the port, if false disables it
 *
 * Return:
 *	0 on success
 *	-ENODEV on port look-up failure
 */
int notrace __cci_control_port_by_device(struct device_node *dn, bool enable)
{
	int port;

	if (!dn)
		return -ENODEV;

	port = __cci_ace_get_port(dn, ACE_LITE_PORT);
	if (WARN_ONCE(port < 0, "node %s ACE lite port look-up failure\n",
				dn->full_name))
		return -ENODEV;
	cci_port_control(port, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(__cci_control_port_by_device);

/**
 * __cci_control_port_by_index() - function to control a CCI port by port index
 *
 * @port: port index previously retrieved with cci_ace_get_port()
 * @enable: if true enables the port, if false disables it
 *
 * Return:
 *	0 on success
 *	-ENODEV on port index out of range
 *	-EPERM if operation carried out on an ACE PORT
 */
int notrace __cci_control_port_by_index(u32 port, bool enable)
{
	if (port >= nb_cci_ports || ports[port].type == ACE_INVALID_PORT)
		return -ENODEV;
	/*
	 * CCI control for ports connected to CPUS is extremely fragile
	 * and must be made to go through a specific and controlled
	 * interface (ie cci_disable_port_by_cpu(); control by general purpose
	 * indexing is therefore disabled for ACE ports.
	 */
	if (ports[port].type == ACE_PORT)
		return -EPERM;

	cci_port_control(port, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(__cci_control_port_by_index);

static const struct cci_nb_ports cci400_ports = {
	.nb_ace = 2,
	.nb_ace_lite = 3
};

static const struct of_device_id arm_cci_matches[] = {
	{.compatible = "arm,cci-400", .data = &cci400_ports },
	{},
};

static const struct of_device_id arm_cci_ctrl_if_matches[] = {
	{.compatible = "arm,cci-400-ctrl-if", },
	{},
};

static int __init cci_probe(void)
{
	struct cci_nb_ports const *cci_config;
	int ret, i, nb_ace = 0, nb_ace_lite = 0;
	struct device_node *np, *cp;
	struct resource res;
	const char *match_str;
	bool is_ace;

	np = of_find_matching_node(NULL, arm_cci_matches);
	if (!np)
		return -ENODEV;

	cci_config = of_match_node(arm_cci_matches, np)->data;
	if (!cci_config)
		return -ENODEV;

	nb_cci_ports = cci_config->nb_ace + cci_config->nb_ace_lite;

	ports = kcalloc(sizeof(*ports), nb_cci_ports, GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	ret = of_address_to_resource(np, 0, &res);
	if (!ret) {
		cci_ctrl_base = ioremap(res.start, resource_size(&res));
		cci_ctrl_phys =	res.start;
	}
	if (ret || !cci_ctrl_base) {
		WARN(1, "unable to ioremap CCI ctrl\n");
		ret = -ENXIO;
		goto memalloc_err;
	}

	for_each_child_of_node(np, cp) {
		if (!of_match_node(arm_cci_ctrl_if_matches, cp))
			continue;

		i = nb_ace + nb_ace_lite;

		if (i >= nb_cci_ports)
			break;

		if (of_property_read_string(cp, "interface-type",
					&match_str)) {
			WARN(1, "node %s missing interface-type property\n",
				  cp->full_name);
			continue;
		}
		is_ace = strcmp(match_str, "ace") == 0;
		if (!is_ace && strcmp(match_str, "ace-lite")) {
			WARN(1, "node %s containing invalid interface-type property, skipping it\n",
					cp->full_name);
			continue;
		}

		ret = of_address_to_resource(cp, 0, &res);
		if (!ret) {
			ports[i].base = ioremap(res.start, resource_size(&res));
			ports[i].phys = res.start;
		}
		if (ret || !ports[i].base) {
			WARN(1, "unable to ioremap CCI port %d\n", i);
			continue;
		}

		if (is_ace) {
			if (WARN_ON(nb_ace >= cci_config->nb_ace))
				continue;
			ports[i].type = ACE_PORT;
			++nb_ace;
		} else {
			if (WARN_ON(nb_ace_lite >= cci_config->nb_ace_lite))
				continue;
			ports[i].type = ACE_LITE_PORT;
			++nb_ace_lite;
		}
		ports[i].dn = cp;
	}

	 /* initialize a stashed array of ACE ports to speed-up look-up */
	cci_ace_init_ports();

	/*
	 * Multi-cluster systems may need this data when non-coherent, during
	 * cluster power-up/power-down. Make sure it reaches main memory.
	 */
	sync_cache_w(&cci_ctrl_base);
	sync_cache_w(&cci_ctrl_phys);
	sync_cache_w(&ports);
	sync_cache_w(&cpu_port);
	__sync_cache_range_w(ports, sizeof(*ports) * nb_cci_ports);
	pr_info("ARM CCI driver probed\n");
	return 0;

memalloc_err:

	kfree(ports);
	return ret;
}

static int cci_init_status = -EAGAIN;
static DEFINE_MUTEX(cci_probing);

static int __init cci_init(void)
{
	if (cci_init_status != -EAGAIN)
		return cci_init_status;

	mutex_lock(&cci_probing);
	if (cci_init_status == -EAGAIN)
		cci_init_status = cci_probe();
	mutex_unlock(&cci_probing);
	return cci_init_status;
}

/*
 * To sort out early init calls ordering a helper function is provided to
 * check if the CCI driver has beed initialized. Function check if the driver
 * has been initialized, if not it calls the init function that probes
 * the driver and updates the return value.
 */
bool __init cci_probed(void)
{
	return cci_init() == 0;
}
EXPORT_SYMBOL_GPL(cci_probed);

early_initcall(cci_init);
core_initcall(cci_pmu_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM CCI support");
