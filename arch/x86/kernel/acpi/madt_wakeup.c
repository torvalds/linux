// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/pgtable.h>
#include <linux/sched/hotplug.h>
#include <asm/apic.h>
#include <asm/barrier.h>
#include <asm/init.h>
#include <asm/intel_pt.h>
#include <asm/nmi.h>
#include <asm/processor.h>
#include <asm/reboot.h>

/* Physical address of the Multiprocessor Wakeup Structure mailbox */
static u64 acpi_mp_wake_mailbox_paddr __ro_after_init;

/* Virtual address of the Multiprocessor Wakeup Structure mailbox */
static struct acpi_madt_multiproc_wakeup_mailbox *acpi_mp_wake_mailbox;

static u64 acpi_mp_pgd __ro_after_init;
static u64 acpi_mp_reset_vector_paddr __ro_after_init;

static void acpi_mp_stop_this_cpu(void)
{
	asm_acpi_mp_play_dead(acpi_mp_reset_vector_paddr, acpi_mp_pgd);
}

static void acpi_mp_play_dead(void)
{
	play_dead_common();
	asm_acpi_mp_play_dead(acpi_mp_reset_vector_paddr, acpi_mp_pgd);
}

static void acpi_mp_cpu_die(unsigned int cpu)
{
	u32 apicid = per_cpu(x86_cpu_to_apicid, cpu);
	unsigned long timeout;

	/*
	 * Use TEST mailbox command to prove that BIOS got control over
	 * the CPU before declaring it dead.
	 *
	 * BIOS has to clear 'command' field of the mailbox.
	 */
	acpi_mp_wake_mailbox->apic_id = apicid;
	smp_store_release(&acpi_mp_wake_mailbox->command,
			  ACPI_MP_WAKE_COMMAND_TEST);

	/* Don't wait longer than a second. */
	timeout = USEC_PER_SEC;
	while (READ_ONCE(acpi_mp_wake_mailbox->command) && --timeout)
		udelay(1);

	if (!timeout)
		pr_err("Failed to hand over CPU %d to BIOS\n", cpu);
}

/* The argument is required to match type of x86_mapping_info::alloc_pgt_page */
static void __init *alloc_pgt_page(void *dummy)
{
	return memblock_alloc(PAGE_SIZE, PAGE_SIZE);
}

static void __init free_pgt_page(void *pgt, void *dummy)
{
	return memblock_free(pgt, PAGE_SIZE);
}

static int __init acpi_mp_setup_reset(u64 reset_vector)
{
	struct x86_mapping_info info = {
		.alloc_pgt_page = alloc_pgt_page,
		.free_pgt_page	= free_pgt_page,
		.page_flag      = __PAGE_KERNEL_LARGE_EXEC,
		.kernpg_flag    = _KERNPG_TABLE_NOENC,
	};
	unsigned long mstart, mend;
	pgd_t *pgd;

	pgd = alloc_pgt_page(NULL);
	if (!pgd)
		return -ENOMEM;

	for (int i = 0; i < nr_pfn_mapped; i++) {
		mstart = pfn_mapped[i].start << PAGE_SHIFT;
		mend   = pfn_mapped[i].end << PAGE_SHIFT;
		if (kernel_ident_mapping_init(&info, pgd, mstart, mend)) {
			kernel_ident_mapping_free(&info, pgd);
			return -ENOMEM;
		}
	}

	mstart = PAGE_ALIGN_DOWN(reset_vector);
	mend = mstart + PAGE_SIZE;
	if (kernel_ident_mapping_init(&info, pgd, mstart, mend)) {
		kernel_ident_mapping_free(&info, pgd);
		return -ENOMEM;
	}

	/*
	 * Make sure asm_acpi_mp_play_dead() is present in the identity mapping
	 * at the same place as in the kernel page tables.
	 * asm_acpi_mp_play_dead() switches to the identity mapping and the
	 * function must be present at the same spot in the virtual address space
	 * before and after switching page tables.
	 */
	info.offset = __START_KERNEL_map - phys_base;
	mstart = PAGE_ALIGN_DOWN(__pa(asm_acpi_mp_play_dead));
	mend = mstart + PAGE_SIZE;
	if (kernel_ident_mapping_init(&info, pgd, mstart, mend)) {
		kernel_ident_mapping_free(&info, pgd);
		return -ENOMEM;
	}

	smp_ops.play_dead = acpi_mp_play_dead;
	smp_ops.stop_this_cpu = acpi_mp_stop_this_cpu;
	smp_ops.cpu_die = acpi_mp_cpu_die;

	acpi_mp_reset_vector_paddr = reset_vector;
	acpi_mp_pgd = __pa(pgd);

	return 0;
}

static int acpi_wakeup_cpu(u32 apicid, unsigned long start_ip, unsigned int cpu)
{
	if (!acpi_mp_wake_mailbox_paddr) {
		pr_warn_once("No MADT mailbox: cannot bringup secondary CPUs. Booting with kexec?\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Remap mailbox memory only for the first call to acpi_wakeup_cpu().
	 *
	 * Wakeup of secondary CPUs is fully serialized in the core code.
	 * No need to protect acpi_mp_wake_mailbox from concurrent accesses.
	 */
	if (!acpi_mp_wake_mailbox) {
		acpi_mp_wake_mailbox = memremap(acpi_mp_wake_mailbox_paddr,
						sizeof(*acpi_mp_wake_mailbox),
						MEMREMAP_WB);
	}

	/*
	 * Mailbox memory is shared between the firmware and OS. Firmware will
	 * listen on mailbox command address, and once it receives the wakeup
	 * command, the CPU associated with the given apicid will be booted.
	 *
	 * The value of 'apic_id' and 'wakeup_vector' must be visible to the
	 * firmware before the wakeup command is visible.  smp_store_release()
	 * ensures ordering and visibility.
	 */
	acpi_mp_wake_mailbox->apic_id	    = apicid;
	acpi_mp_wake_mailbox->wakeup_vector = start_ip;
	smp_store_release(&acpi_mp_wake_mailbox->command,
			  ACPI_MP_WAKE_COMMAND_WAKEUP);

	/*
	 * Wait for the CPU to wake up.
	 *
	 * The CPU being woken up is essentially in a spin loop waiting to be
	 * woken up. It should not take long for it wake up and acknowledge by
	 * zeroing out ->command.
	 *
	 * ACPI specification doesn't provide any guidance on how long kernel
	 * has to wait for a wake up acknowledgment. It also doesn't provide
	 * a way to cancel a wake up request if it takes too long.
	 *
	 * In TDX environment, the VMM has control over how long it takes to
	 * wake up secondary. It can postpone scheduling secondary vCPU
	 * indefinitely. Giving up on wake up request and reporting error opens
	 * possible attack vector for VMM: it can wake up a secondary CPU when
	 * kernel doesn't expect it. Wait until positive result of the wake up
	 * request.
	 */
	while (READ_ONCE(acpi_mp_wake_mailbox->command))
		cpu_relax();

	return 0;
}

static void acpi_mp_disable_offlining(struct acpi_madt_multiproc_wakeup *mp_wake)
{
	cpu_hotplug_disable_offlining();

	/*
	 * ACPI MADT doesn't allow to offline a CPU after it was onlined. This
	 * limits kexec: the second kernel won't be able to use more than one CPU.
	 *
	 * To prevent a kexec kernel from onlining secondary CPUs invalidate the
	 * mailbox address in the ACPI MADT wakeup structure which prevents a
	 * kexec kernel to use it.
	 *
	 * This is safe as the booting kernel has the mailbox address cached
	 * already and acpi_wakeup_cpu() uses the cached value to bring up the
	 * secondary CPUs.
	 *
	 * Note: This is a Linux specific convention and not covered by the
	 *       ACPI specification.
	 */
	mp_wake->mailbox_address = 0;
}

int __init acpi_parse_mp_wake(union acpi_subtable_headers *header,
			      const unsigned long end)
{
	struct acpi_madt_multiproc_wakeup *mp_wake;

	mp_wake = (struct acpi_madt_multiproc_wakeup *)header;

	/*
	 * Cannot use the standard BAD_MADT_ENTRY() to sanity check the @mp_wake
	 * entry.  'sizeof (struct acpi_madt_multiproc_wakeup)' can be larger
	 * than the actual size of the MP wakeup entry in ACPI table because the
	 * 'reset_vector' is only available in the V1 MP wakeup structure.
	 */
	if (!mp_wake)
		return -EINVAL;
	if (end - (unsigned long)mp_wake < ACPI_MADT_MP_WAKEUP_SIZE_V0)
		return -EINVAL;
	if (mp_wake->header.length < ACPI_MADT_MP_WAKEUP_SIZE_V0)
		return -EINVAL;

	acpi_table_print_madt_entry(&header->common);

	acpi_mp_wake_mailbox_paddr = mp_wake->mailbox_address;

	if (mp_wake->version >= ACPI_MADT_MP_WAKEUP_VERSION_V1 &&
	    mp_wake->header.length >= ACPI_MADT_MP_WAKEUP_SIZE_V1) {
		if (acpi_mp_setup_reset(mp_wake->reset_vector)) {
			pr_warn("Failed to setup MADT reset vector\n");
			acpi_mp_disable_offlining(mp_wake);
		}
	} else {
		/*
		 * CPU offlining requires version 1 of the ACPI MADT wakeup
		 * structure.
		 */
		acpi_mp_disable_offlining(mp_wake);
	}

	apic_update_callback(wakeup_secondary_cpu_64, acpi_wakeup_cpu);

	return 0;
}
