/* provide some functions which dump the trace buffer, in a nice way for people
 * to read it, and understand what is going on
 *
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/kernel.h>
#include <linux/hardirq.h>
#include <linux/thread_info.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <asm/dma.h>
#include <asm/trace.h>
#include <asm/fixed_code.h>
#include <asm/traps.h>

#ifdef CONFIG_DEBUG_VERBOSE
#define verbose_printk(fmt, arg...) \
	printk(fmt, ##arg)
#else
#define verbose_printk(fmt, arg...) \
	({ if (0) printk(fmt, ##arg); 0; })
#endif


void decode_address(char *buf, unsigned long address)
{
#ifdef CONFIG_DEBUG_VERBOSE
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long flags, offset;
	unsigned char in_atomic = (bfin_read_IPEND() & 0x10) || in_atomic();
	struct rb_node *n;

#ifdef CONFIG_KALLSYMS
	unsigned long symsize;
	const char *symname;
	char *modname;
	char *delim = ":";
	char namebuf[128];
#endif

	buf += sprintf(buf, "<0x%08lx> ", address);

#ifdef CONFIG_KALLSYMS
	/* look up the address and see if we are in kernel space */
	symname = kallsyms_lookup(address, &symsize, &offset, &modname, namebuf);

	if (symname) {
		/* yeah! kernel space! */
		if (!modname)
			modname = delim = "";
		sprintf(buf, "{ %s%s%s%s + 0x%lx }",
			delim, modname, delim, symname,
			(unsigned long)offset);
		return;
	}
#endif

	if (address >= FIXED_CODE_START && address < FIXED_CODE_END) {
		/* Problem in fixed code section? */
		strcat(buf, "/* Maybe fixed code section */");
		return;

	} else if (address < CONFIG_BOOT_LOAD) {
		/* Problem somewhere before the kernel start address */
		strcat(buf, "/* Maybe null pointer? */");
		return;

	} else if (address >= COREMMR_BASE) {
		strcat(buf, "/* core mmrs */");
		return;

	} else if (address >= SYSMMR_BASE) {
		strcat(buf, "/* system mmrs */");
		return;

	} else if (address >= L1_ROM_START && address < L1_ROM_START + L1_ROM_LENGTH) {
		strcat(buf, "/* on-chip L1 ROM */");
		return;
	}

	/*
	 * Don't walk any of the vmas if we are oopsing, it has been known
	 * to cause problems - corrupt vmas (kernel crashes) cause double faults
	 */
	if (oops_in_progress) {
		strcat(buf, "/* kernel dynamic memory (maybe user-space) */");
		return;
	}

	/* looks like we're off in user-land, so let's walk all the
	 * mappings of all our processes and see if we can't be a whee
	 * bit more specific
	 */
	write_lock_irqsave(&tasklist_lock, flags);
	for_each_process(p) {
		mm = (in_atomic ? p->mm : get_task_mm(p));
		if (!mm)
			continue;

		if (!down_read_trylock(&mm->mmap_sem)) {
			if (!in_atomic)
				mmput(mm);
			continue;
		}

		for (n = rb_first(&mm->mm_rb); n; n = rb_next(n)) {
			struct vm_area_struct *vma;

			vma = rb_entry(n, struct vm_area_struct, vm_rb);

			if (address >= vma->vm_start && address < vma->vm_end) {
				char _tmpbuf[256];
				char *name = p->comm;
				struct file *file = vma->vm_file;

				if (file) {
					char *d_name = d_path(&file->f_path, _tmpbuf,
						      sizeof(_tmpbuf));
					if (!IS_ERR(d_name))
						name = d_name;
				}

				/* FLAT does not have its text aligned to the start of
				 * the map while FDPIC ELF does ...
				 */

				/* before we can check flat/fdpic, we need to
				 * make sure current is valid
				 */
				if ((unsigned long)current >= FIXED_CODE_START &&
				    !((unsigned long)current & 0x3)) {
					if (current->mm &&
					    (address > current->mm->start_code) &&
					    (address < current->mm->end_code))
						offset = address - current->mm->start_code;
					else
						offset = (address - vma->vm_start) +
							 (vma->vm_pgoff << PAGE_SHIFT);

					sprintf(buf, "[ %s + 0x%lx ]", name, offset);
				} else
					sprintf(buf, "[ %s vma:0x%lx-0x%lx]",
						name, vma->vm_start, vma->vm_end);

				up_read(&mm->mmap_sem);
				if (!in_atomic)
					mmput(mm);

				if (buf[0] == '\0')
					sprintf(buf, "[ %s ] dynamic memory", name);

				goto done;
			}
		}

		up_read(&mm->mmap_sem);
		if (!in_atomic)
			mmput(mm);
	}

	/*
	 * we were unable to find this address anywhere,
	 * or some MMs were skipped because they were in use.
	 */
	sprintf(buf, "/* kernel dynamic memory */");

done:
	write_unlock_irqrestore(&tasklist_lock, flags);
#else
	sprintf(buf, " ");
#endif
}

#define EXPAND_LEN ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 256 - 1)

/*
 * Similar to get_user, do some address checking, then dereference
 * Return true on success, false on bad address
 */
bool get_instruction(unsigned short *val, unsigned short *address)
{
	unsigned long addr = (unsigned long)address;

	/* Check for odd addresses */
	if (addr & 0x1)
		return false;

	/* MMR region will never have instructions */
	if (addr >= SYSMMR_BASE)
		return false;

	switch (bfin_mem_access_type(addr, 2)) {
	case BFIN_MEM_ACCESS_CORE:
	case BFIN_MEM_ACCESS_CORE_ONLY:
		*val = *address;
		return true;
	case BFIN_MEM_ACCESS_DMA:
		dma_memcpy(val, address, 2);
		return true;
	case BFIN_MEM_ACCESS_ITEST:
		isram_memcpy(val, address, 2);
		return true;
	default: /* invalid access */
		return false;
	}
}

/*
 * decode the instruction if we are printing out the trace, as it
 * makes things easier to follow, without running it through objdump
 * These are the normal instructions which cause change of flow, which
 * would be at the source of the trace buffer
 */
#if defined(CONFIG_DEBUG_VERBOSE) && defined(CONFIG_DEBUG_BFIN_HWTRACE_ON)
static void decode_instruction(unsigned short *address)
{
	unsigned short opcode;

	if (get_instruction(&opcode, address)) {
		if (opcode == 0x0010)
			verbose_printk("RTS");
		else if (opcode == 0x0011)
			verbose_printk("RTI");
		else if (opcode == 0x0012)
			verbose_printk("RTX");
		else if (opcode == 0x0013)
			verbose_printk("RTN");
		else if (opcode == 0x0014)
			verbose_printk("RTE");
		else if (opcode == 0x0025)
			verbose_printk("EMUEXCPT");
		else if (opcode >= 0x0040 && opcode <= 0x0047)
			verbose_printk("STI R%i", opcode & 7);
		else if (opcode >= 0x0050 && opcode <= 0x0057)
			verbose_printk("JUMP (P%i)", opcode & 7);
		else if (opcode >= 0x0060 && opcode <= 0x0067)
			verbose_printk("CALL (P%i)", opcode & 7);
		else if (opcode >= 0x0070 && opcode <= 0x0077)
			verbose_printk("CALL (PC+P%i)", opcode & 7);
		else if (opcode >= 0x0080 && opcode <= 0x0087)
			verbose_printk("JUMP (PC+P%i)", opcode & 7);
		else if (opcode >= 0x0090 && opcode <= 0x009F)
			verbose_printk("RAISE 0x%x", opcode & 0xF);
		else if (opcode >= 0x00A0 && opcode <= 0x00AF)
			verbose_printk("EXCPT 0x%x", opcode & 0xF);
		else if ((opcode >= 0x1000 && opcode <= 0x13FF) || (opcode >= 0x1800 && opcode <= 0x1BFF))
			verbose_printk("IF !CC JUMP");
		else if ((opcode >= 0x1400 && opcode <= 0x17ff) || (opcode >= 0x1c00 && opcode <= 0x1fff))
			verbose_printk("IF CC JUMP");
		else if (opcode >= 0x2000 && opcode <= 0x2fff)
			verbose_printk("JUMP.S");
		else if (opcode >= 0xe080 && opcode <= 0xe0ff)
			verbose_printk("LSETUP");
		else if (opcode >= 0xe200 && opcode <= 0xe2ff)
			verbose_printk("JUMP.L");
		else if (opcode >= 0xe300 && opcode <= 0xe3ff)
			verbose_printk("CALL pcrel");
		else
			verbose_printk("0x%04x", opcode);
	}

}
#endif

void dump_bfin_trace_buffer(void)
{
#ifdef CONFIG_DEBUG_VERBOSE
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags, i = 0;
	char buf[150];
	unsigned short *addr;
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	int j, index;
#endif

	trace_buffer_save(tflags);

	printk(KERN_NOTICE "Hardware Trace:\n");

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	printk(KERN_NOTICE "WARNING: Expanded trace turned on - can not trace exceptions\n");
#endif

	if (likely(bfin_read_TBUFSTAT() & TBUFCNT)) {
		for (; bfin_read_TBUFSTAT() & TBUFCNT; i++) {
			decode_address(buf, (unsigned long)bfin_read_TBUF());
			printk(KERN_NOTICE "%4i Target : %s\n", i, buf);
			addr = (unsigned short *)bfin_read_TBUF();
			decode_address(buf, (unsigned long)addr);
			printk(KERN_NOTICE "     Source : %s ", buf);
			decode_instruction(addr);
			printk("\n");
		}
	}

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	if (trace_buff_offset)
		index = trace_buff_offset / 4;
	else
		index = EXPAND_LEN;

	j = (1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 128;
	while (j) {
		decode_address(buf, software_trace_buff[index]);
		printk(KERN_NOTICE "%4i Target : %s\n", i, buf);
		index -= 1;
		if (index < 0)
			index = EXPAND_LEN;
		decode_address(buf, software_trace_buff[index]);
		printk(KERN_NOTICE "     Source : %s ", buf);
		decode_instruction((unsigned short *)software_trace_buff[index]);
		printk("\n");
		index -= 1;
		if (index < 0)
			index = EXPAND_LEN;
		j--;
		i++;
	}
#endif

	trace_buffer_restore(tflags);
#endif
#endif
}
EXPORT_SYMBOL(dump_bfin_trace_buffer);

void dump_bfin_process(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_VERBOSE
	/* We should be able to look at fp->ipend, but we don't push it on the
	 * stack all the time, so do this until we fix that */
	unsigned int context = bfin_read_IPEND();

	if (oops_in_progress)
		verbose_printk(KERN_EMERG "Kernel OOPS in progress\n");

	if (context & 0x0020 && (fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR)
		verbose_printk(KERN_NOTICE "HW Error context\n");
	else if (context & 0x0020)
		verbose_printk(KERN_NOTICE "Deferred Exception context\n");
	else if (context & 0x3FC0)
		verbose_printk(KERN_NOTICE "Interrupt context\n");
	else if (context & 0x4000)
		verbose_printk(KERN_NOTICE "Deferred Interrupt context\n");
	else if (context & 0x8000)
		verbose_printk(KERN_NOTICE "Kernel process context\n");

	/* Because we are crashing, and pointers could be bad, we check things
	 * pretty closely before we use them
	 */
	if ((unsigned long)current >= FIXED_CODE_START &&
	    !((unsigned long)current & 0x3) && current->pid) {
		verbose_printk(KERN_NOTICE "CURRENT PROCESS:\n");
		if (current->comm >= (char *)FIXED_CODE_START)
			verbose_printk(KERN_NOTICE "COMM=%s PID=%d",
				current->comm, current->pid);
		else
			verbose_printk(KERN_NOTICE "COMM= invalid");

		printk(KERN_CONT " CPU=%d\n", current_thread_info()->cpu);
		if (!((unsigned long)current->mm & 0x3) && (unsigned long)current->mm >= FIXED_CODE_START)
			verbose_printk(KERN_NOTICE
				"TEXT = 0x%p-0x%p        DATA = 0x%p-0x%p\n"
				" BSS = 0x%p-0x%p  USER-STACK = 0x%p\n\n",
				(void *)current->mm->start_code,
				(void *)current->mm->end_code,
				(void *)current->mm->start_data,
				(void *)current->mm->end_data,
				(void *)current->mm->end_data,
				(void *)current->mm->brk,
				(void *)current->mm->start_stack);
		else
			verbose_printk(KERN_NOTICE "invalid mm\n");
	} else
		verbose_printk(KERN_NOTICE
			       "No Valid process in current context\n");
#endif
}

void dump_bfin_mem(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_VERBOSE
	unsigned short *addr, *erraddr, val = 0, err = 0;
	char sti = 0, buf[6];

	erraddr = (void *)fp->pc;

	verbose_printk(KERN_NOTICE "return address: [0x%p]; contents of:", erraddr);

	for (addr = (unsigned short *)((unsigned long)erraddr & ~0xF) - 0x10;
	     addr < (unsigned short *)((unsigned long)erraddr & ~0xF) + 0x10;
	     addr++) {
		if (!((unsigned long)addr & 0xF))
			verbose_printk(KERN_NOTICE "0x%p: ", addr);

		if (!get_instruction(&val, addr)) {
				val = 0;
				sprintf(buf, "????");
		} else
			sprintf(buf, "%04x", val);

		if (addr == erraddr) {
			verbose_printk("[%s]", buf);
			err = val;
		} else
			verbose_printk(" %s ", buf);

		/* Do any previous instructions turn on interrupts? */
		if (addr <= erraddr &&				/* in the past */
		    ((val >= 0x0040 && val <= 0x0047) ||	/* STI instruction */
		      val == 0x017b))				/* [SP++] = RETI */
			sti = 1;
	}

	verbose_printk("\n");

	/* Hardware error interrupts can be deferred */
	if (unlikely(sti && (fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR &&
	    oops_in_progress)){
		verbose_printk(KERN_NOTICE "Looks like this was a deferred error - sorry\n");
#ifndef CONFIG_DEBUG_HWERR
		verbose_printk(KERN_NOTICE
"The remaining message may be meaningless\n"
"You should enable CONFIG_DEBUG_HWERR to get a better idea where it came from\n");
#else
		/* If we are handling only one peripheral interrupt
		 * and current mm and pid are valid, and the last error
		 * was in that user space process's text area
		 * print it out - because that is where the problem exists
		 */
		if ((!(((fp)->ipend & ~0x30) & (((fp)->ipend & ~0x30) - 1))) &&
		     (current->pid && current->mm)) {
			/* And the last RETI points to the current userspace context */
			if ((fp + 1)->pc >= current->mm->start_code &&
			    (fp + 1)->pc <= current->mm->end_code) {
				verbose_printk(KERN_NOTICE "It might be better to look around here :\n");
				verbose_printk(KERN_NOTICE "-------------------------------------------\n");
				show_regs(fp + 1);
				verbose_printk(KERN_NOTICE "-------------------------------------------\n");
			}
		}
#endif
	}
#endif
}

void show_regs(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_VERBOSE
	char buf[150];
	struct irqaction *action;
	unsigned int i;
	unsigned long flags = 0;
	unsigned int cpu = raw_smp_processor_id();
	unsigned char in_atomic = (bfin_read_IPEND() & 0x10) || in_atomic();

	verbose_printk(KERN_NOTICE "\n");
	if (CPUID != bfin_cpuid())
		verbose_printk(KERN_NOTICE "Compiled for cpu family 0x%04x (Rev %d), "
			"but running on:0x%04x (Rev %d)\n",
			CPUID, bfin_compiled_revid(), bfin_cpuid(), bfin_revid());

	verbose_printk(KERN_NOTICE "ADSP-%s-0.%d",
		CPU, bfin_compiled_revid());

	if (bfin_compiled_revid() !=  bfin_revid())
		verbose_printk("(Detected 0.%d)", bfin_revid());

	verbose_printk(" %lu(MHz CCLK) %lu(MHz SCLK) (%s)\n",
		get_cclk()/1000000, get_sclk()/1000000,
#ifdef CONFIG_MPU
		"mpu on"
#else
		"mpu off"
#endif
		);

	verbose_printk(KERN_NOTICE "%s", linux_banner);

	verbose_printk(KERN_NOTICE "\nSEQUENCER STATUS:\t\t%s\n", print_tainted());
	verbose_printk(KERN_NOTICE " SEQSTAT: %08lx  IPEND: %04lx  IMASK: %04lx  SYSCFG: %04lx\n",
		(long)fp->seqstat, fp->ipend, cpu_pda[raw_smp_processor_id()].ex_imask, fp->syscfg);
	if (fp->ipend & EVT_IRPTEN)
		verbose_printk(KERN_NOTICE "  Global Interrupts Disabled (IPEND[4])\n");
	if (!(cpu_pda[raw_smp_processor_id()].ex_imask & (EVT_IVG13 | EVT_IVG12 | EVT_IVG11 |
			EVT_IVG10 | EVT_IVG9 | EVT_IVG8 | EVT_IVG7 | EVT_IVTMR)))
		verbose_printk(KERN_NOTICE "  Peripheral interrupts masked off\n");
	if (!(cpu_pda[raw_smp_processor_id()].ex_imask & (EVT_IVG15 | EVT_IVG14)))
		verbose_printk(KERN_NOTICE "  Kernel interrupts masked off\n");
	if ((fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR) {
		verbose_printk(KERN_NOTICE "  HWERRCAUSE: 0x%lx\n",
			(fp->seqstat & SEQSTAT_HWERRCAUSE) >> 14);
#ifdef EBIU_ERRMST
		/* If the error was from the EBIU, print it out */
		if (bfin_read_EBIU_ERRMST() & CORE_ERROR) {
			verbose_printk(KERN_NOTICE "  EBIU Error Reason  : 0x%04x\n",
				bfin_read_EBIU_ERRMST());
			verbose_printk(KERN_NOTICE "  EBIU Error Address : 0x%08x\n",
				bfin_read_EBIU_ERRADD());
		}
#endif
	}
	verbose_printk(KERN_NOTICE "  EXCAUSE   : 0x%lx\n",
		fp->seqstat & SEQSTAT_EXCAUSE);
	for (i = 2; i <= 15 ; i++) {
		if (fp->ipend & (1 << i)) {
			if (i != 4) {
				decode_address(buf, bfin_read32(EVT0 + 4*i));
				verbose_printk(KERN_NOTICE "  physical IVG%i asserted : %s\n", i, buf);
			} else
				verbose_printk(KERN_NOTICE "  interrupts disabled\n");
		}
	}

	/* if no interrupts are going off, don't print this out */
	if (fp->ipend & ~0x3F) {
		for (i = 0; i < (NR_IRQS - 1); i++) {
			if (!in_atomic)
				raw_spin_lock_irqsave(&irq_desc[i].lock, flags);

			action = irq_desc[i].action;
			if (!action)
				goto unlock;

			decode_address(buf, (unsigned int)action->handler);
			verbose_printk(KERN_NOTICE "  logical irq %3d mapped  : %s", i, buf);
			for (action = action->next; action; action = action->next) {
				decode_address(buf, (unsigned int)action->handler);
				verbose_printk(", %s", buf);
			}
			verbose_printk("\n");
unlock:
			if (!in_atomic)
				raw_spin_unlock_irqrestore(&irq_desc[i].lock, flags);
		}
	}

	decode_address(buf, fp->rete);
	verbose_printk(KERN_NOTICE " RETE: %s\n", buf);
	decode_address(buf, fp->retn);
	verbose_printk(KERN_NOTICE " RETN: %s\n", buf);
	decode_address(buf, fp->retx);
	verbose_printk(KERN_NOTICE " RETX: %s\n", buf);
	decode_address(buf, fp->rets);
	verbose_printk(KERN_NOTICE " RETS: %s\n", buf);
	decode_address(buf, fp->pc);
	verbose_printk(KERN_NOTICE " PC  : %s\n", buf);

	if (((long)fp->seqstat &  SEQSTAT_EXCAUSE) &&
	    (((long)fp->seqstat & SEQSTAT_EXCAUSE) != VEC_HWERR)) {
		decode_address(buf, cpu_pda[cpu].dcplb_fault_addr);
		verbose_printk(KERN_NOTICE "DCPLB_FAULT_ADDR: %s\n", buf);
		decode_address(buf, cpu_pda[cpu].icplb_fault_addr);
		verbose_printk(KERN_NOTICE "ICPLB_FAULT_ADDR: %s\n", buf);
	}

	verbose_printk(KERN_NOTICE "PROCESSOR STATE:\n");
	verbose_printk(KERN_NOTICE " R0 : %08lx    R1 : %08lx    R2 : %08lx    R3 : %08lx\n",
		fp->r0, fp->r1, fp->r2, fp->r3);
	verbose_printk(KERN_NOTICE " R4 : %08lx    R5 : %08lx    R6 : %08lx    R7 : %08lx\n",
		fp->r4, fp->r5, fp->r6, fp->r7);
	verbose_printk(KERN_NOTICE " P0 : %08lx    P1 : %08lx    P2 : %08lx    P3 : %08lx\n",
		fp->p0, fp->p1, fp->p2, fp->p3);
	verbose_printk(KERN_NOTICE " P4 : %08lx    P5 : %08lx    FP : %08lx    SP : %08lx\n",
		fp->p4, fp->p5, fp->fp, (long)fp);
	verbose_printk(KERN_NOTICE " LB0: %08lx    LT0: %08lx    LC0: %08lx\n",
		fp->lb0, fp->lt0, fp->lc0);
	verbose_printk(KERN_NOTICE " LB1: %08lx    LT1: %08lx    LC1: %08lx\n",
		fp->lb1, fp->lt1, fp->lc1);
	verbose_printk(KERN_NOTICE " B0 : %08lx    L0 : %08lx    M0 : %08lx    I0 : %08lx\n",
		fp->b0, fp->l0, fp->m0, fp->i0);
	verbose_printk(KERN_NOTICE " B1 : %08lx    L1 : %08lx    M1 : %08lx    I1 : %08lx\n",
		fp->b1, fp->l1, fp->m1, fp->i1);
	verbose_printk(KERN_NOTICE " B2 : %08lx    L2 : %08lx    M2 : %08lx    I2 : %08lx\n",
		fp->b2, fp->l2, fp->m2, fp->i2);
	verbose_printk(KERN_NOTICE " B3 : %08lx    L3 : %08lx    M3 : %08lx    I3 : %08lx\n",
		fp->b3, fp->l3, fp->m3, fp->i3);
	verbose_printk(KERN_NOTICE "A0.w: %08lx   A0.x: %08lx   A1.w: %08lx   A1.x: %08lx\n",
		fp->a0w, fp->a0x, fp->a1w, fp->a1x);

	verbose_printk(KERN_NOTICE "USP : %08lx  ASTAT: %08lx\n",
		rdusp(), fp->astat);

	verbose_printk(KERN_NOTICE "\n");
#endif
}
