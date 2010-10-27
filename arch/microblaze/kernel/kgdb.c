/*
 * Microblaze KGDB support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>
#include <asm/pvr.h>

#define GDB_REG		0
#define GDB_PC		32
#define GDB_MSR		33
#define GDB_EAR		34
#define GDB_ESR		35
#define GDB_FSR		36
#define GDB_BTR		37
#define GDB_PVR		38
#define GDB_REDR	50
#define GDB_RPID	51
#define GDB_RZPR	52
#define GDB_RTLBX	53
#define GDB_RTLBSX	54 /* mfs can't read it */
#define GDB_RTLBLO	55
#define GDB_RTLBHI	56

/* keep pvr separately because it is unchangeble */
struct pvr_s pvr;

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	int i;
	unsigned long *pt_regb = (unsigned long *)regs;
	int temp;
	/* registers r0 - r31, pc, msr, ear, esr, fsr + do not save pt_mode */
	for (i = 0; i < (sizeof(struct pt_regs) / 4) - 1; i++)
		gdb_regs[i] = pt_regb[i];

	/* Branch target register can't be changed */
	__asm__ __volatile__ ("mfs %0, rbtr;" : "=r"(temp) : );
	gdb_regs[GDB_BTR] = temp;

	/* pvr part  - we have 11 pvr regs */
	for (i = 0; i < sizeof(struct pvr_s)/4; i++)
		gdb_regs[GDB_PVR + i] = pvr.pvr[i];

	/* read special registers - can't be changed */
	__asm__ __volatile__ ("mfs %0, redr;" : "=r"(temp) : );
	gdb_regs[GDB_REDR] = temp;
	__asm__ __volatile__ ("mfs %0, rpid;" : "=r"(temp) : );
	gdb_regs[GDB_RPID] = temp;
	__asm__ __volatile__ ("mfs %0, rzpr;" : "=r"(temp) : );
	gdb_regs[GDB_RZPR] = temp;
	__asm__ __volatile__ ("mfs %0, rtlbx;" : "=r"(temp) : );
	gdb_regs[GDB_RTLBX] = temp;
	__asm__ __volatile__ ("mfs %0, rtlblo;" : "=r"(temp) : );
	gdb_regs[GDB_RTLBLO] = temp;
	__asm__ __volatile__ ("mfs %0, rtlbhi;" : "=r"(temp) : );
	gdb_regs[GDB_RTLBHI] = temp;
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	int i;
	unsigned long *pt_regb = (unsigned long *)regs;

	/* pt_regs and gdb_regs have the same 37 values.
	 * The rest of gdb_regs are unused and can't be changed.
	 * r0 register value can't be changed too. */
	for (i = 1; i < (sizeof(struct pt_regs) / 4) - 1; i++)
		pt_regb[i] = gdb_regs[i];
}

void microblaze_kgdb_break(struct pt_regs *regs)
{
	if (kgdb_handle_exception(1, SIGTRAP, 0, regs) != 0)
		return 0;

	/* Jump over the first arch_kgdb_breakpoint which is barrier to
	 * get kgdb work. The same solution is used for powerpc */
	if (*(u32 *) (regs->pc) == *(u32 *) (&arch_kgdb_ops.gdb_bpt_instr))
		regs->pc += BREAK_INSTR_SIZE;
}

/* untested */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	int i;
	unsigned long *pt_regb = (unsigned long *)(p->thread.regs);

	/* registers r0 - r31, pc, msr, ear, esr, fsr + do not save pt_mode */
	for (i = 0; i < (sizeof(struct pt_regs) / 4) - 1; i++)
		gdb_regs[i] = pt_regb[i];

	/* pvr part  - we have 11 pvr regs */
	for (i = 0; i < sizeof(struct pvr_s)/4; i++)
		gdb_regs[GDB_PVR + i] = pvr.pvr[i];
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->pc = ip;
}

int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	char *ptr;
	unsigned long address;
	int cpu = smp_processor_id();

	switch (remcom_in_buffer[0]) {
	case 'c':
		/* handle the optional parameter */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &address))
			regs->pc = address;

		return 0;
	}
	return -1; /* this means that we do not want to exit from the handler */
}

int kgdb_arch_init(void)
{
	get_pvr(&pvr); /* Fill PVR structure */
	return 0;
}

void kgdb_arch_exit(void)
{
	/* Nothing to do */
}

/*
 * Global data
 */
struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0xba, 0x0c, 0x00, 0x18}, /* brki r16, 0x18 */
};
