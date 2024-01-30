// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/edac.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/interrupt.h>
#include <linux/panic_notifier.h>
#include <linux/of_irq.h>

#include <asm/cputype.h>

#include "edac_mc.h"
#include "edac_device.h"

#ifdef CONFIG_EDAC_KRYO_ARM64_PANIC_ON_UE
#define ARM64_ERP_PANIC_ON_UE 1
#else
#define ARM64_ERP_PANIC_ON_UE 0
#endif

#define L1_SILVER_BIT 0x0
#define L2_SILVER_BIT 0x1
#define L3_BIT 0x2

#define QCOM_CPU_PART_KRYO4XX_GOLD 0x804
#define QCOM_CPU_PART_KRYO5XX_GOLD 0xD0D
#define QCOM_CPU_PART_A78_GOLD 0xD4B
#define QCOM_CPU_PART_KRYO4XX_SILVER_V1 0x803
#define QCOM_CPU_PART_KRYO4XX_SILVER_V2 0x805

#define QCOM_CPU_PART_KRYO6XX_SILVER_V1 0xD05
#define QCOM_CPU_PART_KRYO6XX_GOLDPLUS 0xD44

#define L1_GOLD_IC_BIT 0x1
#define L1_GOLD_DC_BIT 0x4
#define L2_GOLD_BIT 0x8
#define L2_GOLD_TLB_BIT 0x2

#define L1 0x0
#define L2 0x1
#define L3 0x2

#define EDAC_CPU	"kryo_edac"

#define KRYO_ERRXSTATUS_VALID(a)	((a >> 30) & 0x1)
#define KRYO_ERRXSTATUS_UE(a)	((a >> 29) & 0x1)
#define KRYO_ERRXSTATUS_SERR(a)	(a & 0xFF)

#define KRYO_ERRXMISC_LVL(a)		((a >> 1) & 0x7)
#define KRYO_ERRXMISC_LVL_GOLD(a)	(a & 0xF)
#define KRYO_ERRXMISC_WAY(a)		((a >> 28) & 0xF)

static inline void set_errxctlr_el1(void)
{
	u64 val = 0x10f;

	asm volatile("msr s3_0_c5_c4_1, %0" : : "r" (val));
}

static inline void set_errxmisc_overflow(void)
{
	u64 val = 0x7F7F00000000ULL;

	asm volatile("msr s3_0_c5_c5_0, %0" : : "r" (val));
}

static inline void write_errselr_el1(u64 val)
{
	asm volatile("msr s3_0_c5_c3_1, %0" : : "r" (val));
}

static inline u64 read_errxstatus_el1(void)
{
	u64 val;

	asm volatile("mrs %0, s3_0_c5_c4_2" : "=r" (val));
	return val;
}

static inline u64 read_errxmisc_el1(void)
{
	u64 val;

	asm volatile("mrs %0, s3_0_c5_c5_0" : "=r" (val));
	return val;
}

static inline void clear_errxstatus_valid(u64 val)
{
	asm volatile("msr s3_0_c5_c4_2, %0" : : "r" (val));
}

static void kryo_edac_handle_ce(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg)
{
	edac_device_handle_ce(edac_dev, inst_nr, block_nr, msg);
#ifdef CONFIG_EDAC_KRYO_ARM64_PANIC_ON_CE
	panic("EDAC %s CE: %s\n", edac_dev->ctl_name, msg);
#endif
}

struct errors_edac {
	const char * const msg;
	void (*func)(struct edac_device_ctl_info *edac_dev,
			int inst_nr, int block_nr, const char *msg);
};

static const struct errors_edac errors[] = {
	{"Kryo L1 Correctable Error", kryo_edac_handle_ce },
	{"Kryo L1 Uncorrectable Error", edac_device_handle_ue },
	{"Kryo L2 Correctable Error", kryo_edac_handle_ce },
	{"Kryo L2 Uncorrectable Error", edac_device_handle_ue },
	{"L3 Correctable Error", kryo_edac_handle_ce },
	{"L3 Uncorrectable Error", edac_device_handle_ue },
};

#define KRYO_L1_CE 0
#define KRYO_L1_UE 1
#define KRYO_L2_CE 2
#define KRYO_L2_UE 3
#define KRYO_L3_CE 4
#define KRYO_L3_UE 5

#define DATA_BUF_ERR		0x2
#define CACHE_DATA_ERR		0x6
#define CACHE_TAG_DIRTY_ERR	0x7
#define TLB_PARITY_ERR_DATA	0x8
#define TLB_PARITY_ERR_TAG	0x9
#define BUS_ERROR		0x12

struct erp_drvdata {
	struct edac_device_ctl_info *edev_ctl;
	struct erp_drvdata __percpu *erp_cpu_drvdata;
	struct notifier_block nb_pm;
	struct notifier_block nb_panic;
	int ppi;
};

static struct erp_drvdata *panic_handler_drvdata;

static DEFINE_SPINLOCK(local_handler_lock);

static void l1_l2_irq_enable(void *info)
{
	int irq = *(int *)info;

	enable_percpu_irq(irq, irq_get_trigger_type(irq));
}

static void l1_l2_irq_disable(void *info)
{
	int irq = *(int *)info;

	disable_percpu_irq(irq);
}

static int request_erp_irq(struct platform_device *pdev, const char *propname,
			const char *desc, irq_handler_t handler,
			void *ed, int percpu)
{
	int rc;
	struct erp_drvdata *drv = ed;
	struct erp_drvdata *temp = NULL;
	int irq;

	irq = platform_get_irq_byname(pdev, propname);
	if (irq < 0) {
		pr_err("ARM64 CPU ERP: Could not find <%s> IRQ property. Proceeding anyway.\n",
			propname);
		goto out;
	}

	if (!percpu) {
		rc = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					       handler,
					       IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					       desc,
					       ed);

		if (rc) {
			pr_err("ARM64 CPU ERP: Failed to request IRQ %d: %d (%s / %s). Proceeding anyway.\n",
				irq, rc, propname, desc);
			goto out;
		}

	} else {
		drv->erp_cpu_drvdata = alloc_percpu(struct erp_drvdata);
		if (!drv->erp_cpu_drvdata) {
			pr_err("Failed to allocate percpu erp data\n");
			goto out;
		}

		temp = raw_cpu_ptr(drv->erp_cpu_drvdata);
		temp->erp_cpu_drvdata = drv;

		rc = request_percpu_irq(irq, handler, desc,
				drv->erp_cpu_drvdata);

		if (rc) {
			pr_err("ARM64 CPU ERP: Failed to request IRQ %d: %d (%s / %s). Proceeding anyway.\n",
			       irq, rc, propname, desc);
			goto out_free;
		}

		drv->ppi = irq;
		on_each_cpu(l1_l2_irq_enable, &irq, 1);
	}

	return 0;

out_free:
	free_percpu(drv->erp_cpu_drvdata);
	drv->erp_cpu_drvdata = NULL;
out:
	return -EINVAL;
}

static void dump_err_reg(int errorcode, int level, u64 errxstatus, u64 errxmisc,
	struct edac_device_ctl_info *edev_ctl)
{
	edac_printk(KERN_CRIT, EDAC_CPU, "ERRXSTATUS_EL1: %llx\n", errxstatus);
	edac_printk(KERN_CRIT, EDAC_CPU, "ERRXMISC_EL1: %llx\n", errxmisc);
	edac_printk(KERN_CRIT, EDAC_CPU, "Cache level: L%d\n", level + 1);

	switch (KRYO_ERRXSTATUS_SERR(errxstatus)) {
	case DATA_BUF_ERR:
		edac_printk(KERN_CRIT, EDAC_CPU, "ECC Error from internal data buffer\n");
		break;

	case CACHE_DATA_ERR:
		edac_printk(KERN_CRIT, EDAC_CPU, "ECC Error from cache data RAM\n");
		break;

	case CACHE_TAG_DIRTY_ERR:
		edac_printk(KERN_CRIT, EDAC_CPU, "ECC Error from cache tag or dirty RAM\n");
		break;

	case TLB_PARITY_ERR_DATA:
		edac_printk(KERN_CRIT, EDAC_CPU, "Parity error on TLB DATA RAM\n");
		break;

	case TLB_PARITY_ERR_TAG:
		edac_printk(KERN_CRIT, EDAC_CPU, "Parity error on TLB TAG RAM\n");
		break;

	case BUS_ERROR:
		edac_printk(KERN_CRIT, EDAC_CPU, "Bus Error\n");
		break;
	}

	if (level == L3)
		edac_printk(KERN_CRIT, EDAC_CPU,
			"Way: %d\n", (int) KRYO_ERRXMISC_WAY(errxmisc));
	else
		edac_printk(KERN_CRIT, EDAC_CPU,
			"Way: %d\n", (int) KRYO_ERRXMISC_WAY(errxmisc) >> 2);
	errors[errorcode].func(edev_ctl, smp_processor_id(),
				level, errors[errorcode].msg);
}

static void kryo_parse_l1_l2_cache_error(u64 errxstatus, u64 errxmisc,
	struct edac_device_ctl_info *edev_ctl, int cpu)
{
	int level = 0;
	u32 part_num;

	part_num = read_cpuid_part_number();
	switch (part_num) {
	case QCOM_CPU_PART_KRYO4XX_SILVER_V1:
	case QCOM_CPU_PART_KRYO4XX_SILVER_V2:
	case QCOM_CPU_PART_KRYO6XX_SILVER_V1:
		switch (KRYO_ERRXMISC_LVL(errxmisc)) {
		case L1_SILVER_BIT:
			level = L1;
			break;
		case L2_SILVER_BIT:
			level = L2;
			break;
		default:
			edac_printk(KERN_CRIT, EDAC_CPU,
				"silver cpu:%d unknown error location:%llu\n",
				cpu, KRYO_ERRXMISC_LVL(errxmisc));
		}
		break;
	case QCOM_CPU_PART_KRYO4XX_GOLD:
	case QCOM_CPU_PART_KRYO5XX_GOLD:
	case QCOM_CPU_PART_KRYO6XX_GOLDPLUS:
	case QCOM_CPU_PART_A78_GOLD:
		switch (KRYO_ERRXMISC_LVL_GOLD(errxmisc)) {
		case L1_GOLD_DC_BIT:
		case L1_GOLD_IC_BIT:
			level = L1;
			break;
		case L2_GOLD_BIT:
		case L2_GOLD_TLB_BIT:
			level = L2;
			break;
		default:
			edac_printk(KERN_CRIT, EDAC_CPU,
				"gold cpu:%d unknown error location:%llu\n",
				cpu, KRYO_ERRXMISC_LVL_GOLD(errxmisc));
		}
		break;
	default:
		edac_printk(KERN_CRIT, EDAC_CPU,
			"Error in matching cpu%d with part num:%u\n",
			cpu, part_num);
		return;
	}

	switch (level) {
	case L1:
		if (KRYO_ERRXSTATUS_UE(errxstatus))
			dump_err_reg(KRYO_L1_UE, level, errxstatus, errxmisc,
					edev_ctl);
		else
			dump_err_reg(KRYO_L1_CE, level, errxstatus, errxmisc,
					edev_ctl);
		break;
	case L2:
		if (KRYO_ERRXSTATUS_UE(errxstatus))
			dump_err_reg(KRYO_L2_UE, level, errxstatus, errxmisc,
					edev_ctl);
		else
			dump_err_reg(KRYO_L2_CE, level, errxstatus, errxmisc,
					edev_ctl);
		break;
	default:
		edac_printk(KERN_CRIT, EDAC_CPU, "Unknown KRYO_ERRXMISC_LVL value\n");
	}
}

static void kryo_check_l1_l2_ecc(void *info)
{
	struct edac_device_ctl_info *edev_ctl = info;
	u64 errxstatus = 0;
	u64 errxmisc = 0;
	int cpu = 0;
	unsigned long flags;

	spin_lock_irqsave(&local_handler_lock, flags);
	write_errselr_el1(0);
	errxstatus = read_errxstatus_el1();
	cpu = smp_processor_id();

	if (KRYO_ERRXSTATUS_VALID(errxstatus)) {
		errxmisc = read_errxmisc_el1();
		edac_printk(KERN_CRIT, EDAC_CPU,
		"Kryo CPU%d detected a L1/L2 cache error, errxstatus = %llx, errxmisc = %llx\n",
		cpu, errxstatus, errxmisc);

		kryo_parse_l1_l2_cache_error(errxstatus, errxmisc, edev_ctl,
				cpu);
		clear_errxstatus_valid(errxstatus);
	}
	spin_unlock_irqrestore(&local_handler_lock, flags);
}

static bool l3_is_bus_error(u64 errxstatus)
{
	if (KRYO_ERRXSTATUS_SERR(errxstatus) == BUS_ERROR) {
		edac_printk(KERN_CRIT, EDAC_CPU, "Bus Error\n");
		return true;
	}

	return false;
}

static void kryo_check_l3_scu_error(struct edac_device_ctl_info *edev_ctl)
{
	u64 errxstatus = 0;
	u64 errxmisc = 0;
	unsigned long flags;

	spin_lock_irqsave(&local_handler_lock, flags);
	write_errselr_el1(1);
	errxstatus = read_errxstatus_el1();
	errxmisc = read_errxmisc_el1();

	if (KRYO_ERRXSTATUS_VALID(errxstatus) &&
		KRYO_ERRXMISC_LVL(errxmisc) == L3_BIT) {
		if (l3_is_bus_error(errxstatus)) {
			if (edev_ctl->panic_on_ue) {
				spin_unlock_irqrestore(&local_handler_lock, flags);
				panic("Causing panic due to Bus Error\n");
			}
			goto unlock;
		}
		if (KRYO_ERRXSTATUS_UE(errxstatus)) {
			edac_printk(KERN_CRIT, EDAC_CPU, "Detected L3 uncorrectable error\n");
			dump_err_reg(KRYO_L3_UE, L3, errxstatus, errxmisc,
				edev_ctl);
		} else {
			edac_printk(KERN_CRIT, EDAC_CPU, "Detected L3 correctable error\n");
			dump_err_reg(KRYO_L3_CE, L3, errxstatus, errxmisc,
				edev_ctl);
		}

		clear_errxstatus_valid(errxstatus);
	}
unlock:
	spin_unlock_irqrestore(&local_handler_lock, flags);
}

static int kryo_cpu_panic_notify(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct edac_device_ctl_info *edev_ctl =
				panic_handler_drvdata->edev_ctl;

	edev_ctl->panic_on_ue = 0;

	kryo_check_l3_scu_error(edev_ctl);
	kryo_check_l1_l2_ecc(edev_ctl);

	return NOTIFY_OK;
}

static irqreturn_t kryo_l1_l2_handler(int irq, void *drvdata)
{
	kryo_check_l1_l2_ecc(panic_handler_drvdata->edev_ctl);
	return IRQ_HANDLED;
}

static irqreturn_t kryo_l3_scu_handler(int irq, void *drvdata)
{
	struct erp_drvdata *drv = drvdata;
	struct edac_device_ctl_info *edev_ctl = drv->edev_ctl;

	kryo_check_l3_scu_error(edev_ctl);
	return IRQ_HANDLED;
}

static void initialize_registers(void *info)
{
	set_errxctlr_el1();
	set_errxmisc_overflow();
}

static void init_regs_on_cpu(bool all_cpus)
{
	int cpu;

	write_errselr_el1(0);
	if (all_cpus) {
		for_each_possible_cpu(cpu)
			smp_call_function_single(cpu, initialize_registers,
						NULL, 1);
	} else
		initialize_registers(NULL);

	write_errselr_el1(1);
	initialize_registers(NULL);
}

static int kryo_pmu_cpu_pm_notify(struct notifier_block *self,
				unsigned long action, void *v)
{
	switch (action) {
	case CPU_PM_EXIT:
		init_regs_on_cpu(false);
		kryo_check_l3_scu_error(panic_handler_drvdata->edev_ctl);
		kryo_check_l1_l2_ecc(panic_handler_drvdata->edev_ctl);
		break;
	}

	return NOTIFY_OK;
}

static int kryo_cpu_erp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct erp_drvdata *drv;
	int rc = 0;
	int erp_pass = 0;
	int num_irqs = 0;

	init_regs_on_cpu(true);

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);

	if (!drv)
		return -ENOMEM;

	drv->edev_ctl = edac_device_alloc_ctl_info(0, "cpu",
					num_possible_cpus(), "L", 3, 1, NULL, 0,
					edac_device_alloc_index());

	if (!drv->edev_ctl)
		return -ENOMEM;

	drv->edev_ctl->dev = dev;
	drv->edev_ctl->mod_name = dev_name(dev);
	drv->edev_ctl->dev_name = dev_name(dev);
	drv->edev_ctl->ctl_name = "cache";
	drv->edev_ctl->panic_on_ue = ARM64_ERP_PANIC_ON_UE;
	drv->nb_pm.notifier_call = kryo_pmu_cpu_pm_notify;
	drv->nb_panic.notifier_call = kryo_cpu_panic_notify;
	atomic_notifier_chain_register(&panic_notifier_list,
				       &drv->nb_panic);
	platform_set_drvdata(pdev, drv);

	rc = edac_device_add_device(drv->edev_ctl);
	if (rc)
		goto out_mem;

	panic_handler_drvdata = drv;
	num_irqs = platform_irq_count(pdev);
	if (num_irqs == 0) {
		pr_err("KRYO ERP: No irqs found for error reporting\n");
		rc = -EINVAL;
		goto out_dev;
	}

	if (num_irqs < 0) {
		rc = num_irqs;
		goto out_dev;
	}

	if (!request_erp_irq(pdev, "l1-l2-faultirq",
			"KRYO L1-L2 ECC FAULTIRQ",
			kryo_l1_l2_handler, drv, 1))
		erp_pass++;

	if (!request_erp_irq(pdev, "l3-scu-faultirq",
			"KRYO L3-SCU ECC FAULTIRQ",
			kryo_l3_scu_handler, drv, 0))
		erp_pass++;

	if (!request_erp_irq(pdev, "l3-c0-scu-faultirq",
			"KRYO L3-SCU ECC FAULTIRQ CLUSTER 0",
			kryo_l3_scu_handler, drv, 0))
		erp_pass++;

	if (!request_erp_irq(pdev, "l3-c1-scu-faultirq",
			"KRYO L3-SCU ECC FAULTIRQ CLUSTER 1",
			kryo_l3_scu_handler, drv, 0))
		erp_pass++;

	/* Return if none of the IRQ is valid */
	if (!erp_pass) {
		pr_err("KRYO ERP: Could not request any IRQs. Giving up.\n");
		rc = -ENODEV;
		goto out_dev;
	}

	cpu_pm_register_notifier(&(drv->nb_pm));

	return 0;

out_dev:
	edac_device_del_device(dev);
out_mem:
	edac_device_free_ctl_info(drv->edev_ctl);
	return rc;
}

static int kryo_cpu_erp_remove(struct platform_device *pdev)
{
	struct erp_drvdata *drv = dev_get_drvdata(&pdev->dev);
	struct edac_device_ctl_info *edac_ctl = drv->edev_ctl;

	if (drv->erp_cpu_drvdata != NULL) {
		on_each_cpu(l1_l2_irq_disable, &(drv->ppi), 1);
		free_percpu_irq(drv->ppi, drv->erp_cpu_drvdata);
		free_percpu(drv->erp_cpu_drvdata);
	}

	cpu_pm_unregister_notifier(&(drv->nb_pm));
	edac_device_del_device(edac_ctl->dev);
	edac_device_free_ctl_info(edac_ctl);

	return 0;
}

static const struct of_device_id kryo_cpu_erp_match_table[] = {
	{ .compatible = "arm,arm64-kryo-cpu-erp" },
	{ }
};

static struct platform_driver kryo_cpu_erp_driver = {
	.probe = kryo_cpu_erp_probe,
	.remove = kryo_cpu_erp_remove,
	.driver = {
		.name = "kryo_cpu_cache_erp",
		.of_match_table = of_match_ptr(kryo_cpu_erp_match_table),
	},
};

module_platform_driver(kryo_cpu_erp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kryo EDAC driver");
