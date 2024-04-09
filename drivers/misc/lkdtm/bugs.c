// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests related to logic bugs (e.g. bad dereferences,
 * bad alignment, bad loops, bad locking, bad scheduling, deep stacks, and
 * lockups) along with other things that don't fit well into existing LKDTM
 * test source files.
 */
#include "lkdtm.h"
#include <linux/cpu.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>

#if IS_ENABLED(CONFIG_X86_32) && !IS_ENABLED(CONFIG_UML)
#include <asm/desc.h>
#endif

struct lkdtm_list {
	struct list_head node;
};

/*
 * Make sure our attempts to over run the kernel stack doesn't trigger
 * a compiler warning when CONFIG_FRAME_WARN is set. Then make sure we
 * recurse past the end of THREAD_SIZE by default.
 */
#if defined(CONFIG_FRAME_WARN) && (CONFIG_FRAME_WARN > 0)
#define REC_STACK_SIZE (_AC(CONFIG_FRAME_WARN, UL) / 2)
#else
#define REC_STACK_SIZE (THREAD_SIZE / 8UL)
#endif
#define REC_NUM_DEFAULT ((THREAD_SIZE / REC_STACK_SIZE) * 2)

static int recur_count = REC_NUM_DEFAULT;

static DEFINE_SPINLOCK(lock_me_up);

/*
 * Make sure compiler does not optimize this function or stack frame away:
 * - function marked noinline
 * - stack variables are marked volatile
 * - stack variables are written (memset()) and read (buf[..] passed as arg)
 * - function may have external effects (memzero_explicit())
 * - no tail recursion possible
 */
static int noinline recursive_loop(int remaining)
{
	volatile char buf[REC_STACK_SIZE];
	volatile int ret;

	memset((void *)buf, remaining & 0xFF, sizeof(buf));
	if (!remaining)
		ret = 0;
	else
		ret = recursive_loop((int)buf[remaining % sizeof(buf)] - 1);
	memzero_explicit((void *)buf, sizeof(buf));
	return ret;
}

/* If the depth is negative, use the default, otherwise keep parameter. */
void __init lkdtm_bugs_init(int *recur_param)
{
	if (*recur_param < 0)
		*recur_param = recur_count;
	else
		recur_count = *recur_param;
}

static void lkdtm_PANIC(void)
{
	panic("dumptest");
}

static int panic_stop_irqoff_fn(void *arg)
{
	atomic_t *v = arg;

	/*
	 * As stop_machine() disables interrupts, all CPUs within this function
	 * have interrupts disabled and cannot take a regular IPI.
	 *
	 * The last CPU which enters here will trigger a panic, and as all CPUs
	 * cannot take a regular IPI, we'll only be able to stop secondaries if
	 * smp_send_stop() or crash_smp_send_stop() uses an NMI.
	 */
	if (atomic_inc_return(v) == num_online_cpus())
		panic("panic stop irqoff test");

	for (;;)
		cpu_relax();
}

static void lkdtm_PANIC_STOP_IRQOFF(void)
{
	atomic_t v = ATOMIC_INIT(0);
	stop_machine(panic_stop_irqoff_fn, &v, cpu_online_mask);
}

static void lkdtm_BUG(void)
{
	BUG();
}

static int warn_counter;

static void lkdtm_WARNING(void)
{
	WARN_ON(++warn_counter);
}

static void lkdtm_WARNING_MESSAGE(void)
{
	WARN(1, "Warning message trigger count: %d\n", ++warn_counter);
}

static void lkdtm_EXCEPTION(void)
{
	*((volatile int *) 0) = 0;
}

static void lkdtm_LOOP(void)
{
	for (;;)
		;
}

static void lkdtm_EXHAUST_STACK(void)
{
	pr_info("Calling function with %lu frame size to depth %d ...\n",
		REC_STACK_SIZE, recur_count);
	recursive_loop(recur_count);
	pr_info("FAIL: survived without exhausting stack?!\n");
}

static noinline void __lkdtm_CORRUPT_STACK(void *stack)
{
	memset(stack, '\xff', 64);
}

/* This should trip the stack canary, not corrupt the return address. */
static noinline void lkdtm_CORRUPT_STACK(void)
{
	/* Use default char array length that triggers stack protection. */
	char data[8] __aligned(sizeof(void *));

	pr_info("Corrupting stack containing char array ...\n");
	__lkdtm_CORRUPT_STACK((void *)&data);
}

/* Same as above but will only get a canary with -fstack-protector-strong */
static noinline void lkdtm_CORRUPT_STACK_STRONG(void)
{
	union {
		unsigned short shorts[4];
		unsigned long *ptr;
	} data __aligned(sizeof(void *));

	pr_info("Corrupting stack containing union ...\n");
	__lkdtm_CORRUPT_STACK((void *)&data);
}

static pid_t stack_pid;
static unsigned long stack_addr;

static void lkdtm_REPORT_STACK(void)
{
	volatile uintptr_t magic;
	pid_t pid = task_pid_nr(current);

	if (pid != stack_pid) {
		pr_info("Starting stack offset tracking for pid %d\n", pid);
		stack_pid = pid;
		stack_addr = (uintptr_t)&magic;
	}

	pr_info("Stack offset: %d\n", (int)(stack_addr - (uintptr_t)&magic));
}

static pid_t stack_canary_pid;
static unsigned long stack_canary;
static unsigned long stack_canary_offset;

static noinline void __lkdtm_REPORT_STACK_CANARY(void *stack)
{
	int i = 0;
	pid_t pid = task_pid_nr(current);
	unsigned long *canary = (unsigned long *)stack;
	unsigned long current_offset = 0, init_offset = 0;

	/* Do our best to find the canary in a 16 word window ... */
	for (i = 1; i < 16; i++) {
		canary = (unsigned long *)stack + i;
#ifdef CONFIG_STACKPROTECTOR
		if (*canary == current->stack_canary)
			current_offset = i;
		if (*canary == init_task.stack_canary)
			init_offset = i;
#endif
	}

	if (current_offset == 0) {
		/*
		 * If the canary doesn't match what's in the task_struct,
		 * we're either using a global canary or the stack frame
		 * layout changed.
		 */
		if (init_offset != 0) {
			pr_err("FAIL: global stack canary found at offset %ld (canary for pid %d matches init_task's)!\n",
			       init_offset, pid);
		} else {
			pr_warn("FAIL: did not correctly locate stack canary :(\n");
			pr_expected_config(CONFIG_STACKPROTECTOR);
		}

		return;
	} else if (init_offset != 0) {
		pr_warn("WARNING: found both current and init_task canaries nearby?!\n");
	}

	canary = (unsigned long *)stack + current_offset;
	if (stack_canary_pid == 0) {
		stack_canary = *canary;
		stack_canary_pid = pid;
		stack_canary_offset = current_offset;
		pr_info("Recorded stack canary for pid %d at offset %ld\n",
			stack_canary_pid, stack_canary_offset);
	} else if (pid == stack_canary_pid) {
		pr_warn("ERROR: saw pid %d again -- please use a new pid\n", pid);
	} else {
		if (current_offset != stack_canary_offset) {
			pr_warn("ERROR: canary offset changed from %ld to %ld!?\n",
				stack_canary_offset, current_offset);
			return;
		}

		if (*canary == stack_canary) {
			pr_warn("FAIL: canary identical for pid %d and pid %d at offset %ld!\n",
				stack_canary_pid, pid, current_offset);
		} else {
			pr_info("ok: stack canaries differ between pid %d and pid %d at offset %ld.\n",
				stack_canary_pid, pid, current_offset);
			/* Reset the test. */
			stack_canary_pid = 0;
		}
	}
}

static void lkdtm_REPORT_STACK_CANARY(void)
{
	/* Use default char array length that triggers stack protection. */
	char data[8] __aligned(sizeof(void *)) = { };

	__lkdtm_REPORT_STACK_CANARY((void *)&data);
}

static void lkdtm_UNALIGNED_LOAD_STORE_WRITE(void)
{
	static u8 data[5] __attribute__((aligned(4))) = {1, 2, 3, 4, 5};
	u32 *p;
	u32 val = 0x12345678;

	p = (u32 *)(data + 1);
	if (*p == 0)
		val = 0x87654321;
	*p = val;

	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))
		pr_err("XFAIL: arch has CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS\n");
}

static void lkdtm_SOFTLOCKUP(void)
{
	preempt_disable();
	for (;;)
		cpu_relax();
}

static void lkdtm_HARDLOCKUP(void)
{
	local_irq_disable();
	for (;;)
		cpu_relax();
}

static void lkdtm_SPINLOCKUP(void)
{
	/* Must be called twice to trigger. */
	spin_lock(&lock_me_up);
	/* Let sparse know we intended to exit holding the lock. */
	__release(&lock_me_up);
}

static void __noreturn lkdtm_HUNG_TASK(void)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	BUG();
}

static volatile unsigned int huge = INT_MAX - 2;
static volatile unsigned int ignored;

static void lkdtm_OVERFLOW_SIGNED(void)
{
	int value;

	value = huge;
	pr_info("Normal signed addition ...\n");
	value += 1;
	ignored = value;

	pr_info("Overflowing signed addition ...\n");
	value += 4;
	ignored = value;
}


static void lkdtm_OVERFLOW_UNSIGNED(void)
{
	unsigned int value;

	value = huge;
	pr_info("Normal unsigned addition ...\n");
	value += 1;
	ignored = value;

	pr_info("Overflowing unsigned addition ...\n");
	value += 4;
	ignored = value;
}

/* Intentionally using unannotated flex array definition. */
struct array_bounds_flex_array {
	int one;
	int two;
	char data[];
};

struct array_bounds {
	int one;
	int two;
	char data[8];
	int three;
};

static void lkdtm_ARRAY_BOUNDS(void)
{
	struct array_bounds_flex_array *not_checked;
	struct array_bounds *checked;
	volatile int i;

	not_checked = kmalloc(sizeof(*not_checked) * 2, GFP_KERNEL);
	checked = kmalloc(sizeof(*checked) * 2, GFP_KERNEL);
	if (!not_checked || !checked) {
		kfree(not_checked);
		kfree(checked);
		return;
	}

	pr_info("Array access within bounds ...\n");
	/* For both, touch all bytes in the actual member size. */
	for (i = 0; i < sizeof(checked->data); i++)
		checked->data[i] = 'A';
	/*
	 * For the uninstrumented flex array member, also touch 1 byte
	 * beyond to verify it is correctly uninstrumented.
	 */
	for (i = 0; i < 2; i++)
		not_checked->data[i] = 'A';

	pr_info("Array access beyond bounds ...\n");
	for (i = 0; i < sizeof(checked->data) + 1; i++)
		checked->data[i] = 'B';

	kfree(not_checked);
	kfree(checked);
	pr_err("FAIL: survived array bounds overflow!\n");
	if (IS_ENABLED(CONFIG_UBSAN_BOUNDS))
		pr_expected_config(CONFIG_UBSAN_TRAP);
	else
		pr_expected_config(CONFIG_UBSAN_BOUNDS);
}

struct lkdtm_annotated {
	unsigned long flags;
	int count;
	int array[] __counted_by(count);
};

static volatile int fam_count = 4;

static void lkdtm_FAM_BOUNDS(void)
{
	struct lkdtm_annotated *inst;

	inst = kzalloc(struct_size(inst, array, fam_count + 1), GFP_KERNEL);
	if (!inst) {
		pr_err("FAIL: could not allocate test struct!\n");
		return;
	}

	inst->count = fam_count;
	pr_info("Array access within bounds ...\n");
	inst->array[1] = fam_count;
	ignored = inst->array[1];

	pr_info("Array access beyond bounds ...\n");
	inst->array[fam_count] = fam_count;
	ignored = inst->array[fam_count];

	kfree(inst);

	pr_err("FAIL: survived access of invalid flexible array member index!\n");

	if (!__has_attribute(__counted_by__))
		pr_warn("This is expected since this %s was built with a compiler that does not support __counted_by\n",
			lkdtm_kernel_info);
	else if (IS_ENABLED(CONFIG_UBSAN_BOUNDS))
		pr_expected_config(CONFIG_UBSAN_TRAP);
	else
		pr_expected_config(CONFIG_UBSAN_BOUNDS);
}

static void lkdtm_CORRUPT_LIST_ADD(void)
{
	/*
	 * Initially, an empty list via LIST_HEAD:
	 *	test_head.next = &test_head
	 *	test_head.prev = &test_head
	 */
	LIST_HEAD(test_head);
	struct lkdtm_list good, bad;
	void *target[2] = { };
	void *redirection = &target;

	pr_info("attempting good list addition\n");

	/*
	 * Adding to the list performs these actions:
	 *	test_head.next->prev = &good.node
	 *	good.node.next = test_head.next
	 *	good.node.prev = test_head
	 *	test_head.next = good.node
	 */
	list_add(&good.node, &test_head);

	pr_info("attempting corrupted list addition\n");
	/*
	 * In simulating this "write what where" primitive, the "what" is
	 * the address of &bad.node, and the "where" is the address held
	 * by "redirection".
	 */
	test_head.next = redirection;
	list_add(&bad.node, &test_head);

	if (target[0] == NULL && target[1] == NULL)
		pr_err("Overwrite did not happen, but no BUG?!\n");
	else {
		pr_err("list_add() corruption not detected!\n");
		pr_expected_config(CONFIG_LIST_HARDENED);
	}
}

static void lkdtm_CORRUPT_LIST_DEL(void)
{
	LIST_HEAD(test_head);
	struct lkdtm_list item;
	void *target[2] = { };
	void *redirection = &target;

	list_add(&item.node, &test_head);

	pr_info("attempting good list removal\n");
	list_del(&item.node);

	pr_info("attempting corrupted list removal\n");
	list_add(&item.node, &test_head);

	/* As with the list_add() test above, this corrupts "next". */
	item.node.next = redirection;
	list_del(&item.node);

	if (target[0] == NULL && target[1] == NULL)
		pr_err("Overwrite did not happen, but no BUG?!\n");
	else {
		pr_err("list_del() corruption not detected!\n");
		pr_expected_config(CONFIG_LIST_HARDENED);
	}
}

/* Test that VMAP_STACK is actually allocating with a leading guard page */
static void lkdtm_STACK_GUARD_PAGE_LEADING(void)
{
	const unsigned char *stack = task_stack_page(current);
	const unsigned char *ptr = stack - 1;
	volatile unsigned char byte;

	pr_info("attempting bad read from page below current stack\n");

	byte = *ptr;

	pr_err("FAIL: accessed page before stack! (byte: %x)\n", byte);
}

/* Test that VMAP_STACK is actually allocating with a trailing guard page */
static void lkdtm_STACK_GUARD_PAGE_TRAILING(void)
{
	const unsigned char *stack = task_stack_page(current);
	const unsigned char *ptr = stack + THREAD_SIZE;
	volatile unsigned char byte;

	pr_info("attempting bad read from page above current stack\n");

	byte = *ptr;

	pr_err("FAIL: accessed page after stack! (byte: %x)\n", byte);
}

static void lkdtm_UNSET_SMEP(void)
{
#if IS_ENABLED(CONFIG_X86_64) && !IS_ENABLED(CONFIG_UML)
#define MOV_CR4_DEPTH	64
	void (*direct_write_cr4)(unsigned long val);
	unsigned char *insn;
	unsigned long cr4;
	int i;

	cr4 = native_read_cr4();

	if ((cr4 & X86_CR4_SMEP) != X86_CR4_SMEP) {
		pr_err("FAIL: SMEP not in use\n");
		return;
	}
	cr4 &= ~(X86_CR4_SMEP);

	pr_info("trying to clear SMEP normally\n");
	native_write_cr4(cr4);
	if (cr4 == native_read_cr4()) {
		pr_err("FAIL: pinning SMEP failed!\n");
		cr4 |= X86_CR4_SMEP;
		pr_info("restoring SMEP\n");
		native_write_cr4(cr4);
		return;
	}
	pr_info("ok: SMEP did not get cleared\n");

	/*
	 * To test the post-write pinning verification we need to call
	 * directly into the middle of native_write_cr4() where the
	 * cr4 write happens, skipping any pinning. This searches for
	 * the cr4 writing instruction.
	 */
	insn = (unsigned char *)native_write_cr4;
	OPTIMIZER_HIDE_VAR(insn);
	for (i = 0; i < MOV_CR4_DEPTH; i++) {
		/* mov %rdi, %cr4 */
		if (insn[i] == 0x0f && insn[i+1] == 0x22 && insn[i+2] == 0xe7)
			break;
		/* mov %rdi,%rax; mov %rax, %cr4 */
		if (insn[i]   == 0x48 && insn[i+1] == 0x89 &&
		    insn[i+2] == 0xf8 && insn[i+3] == 0x0f &&
		    insn[i+4] == 0x22 && insn[i+5] == 0xe0)
			break;
	}
	if (i >= MOV_CR4_DEPTH) {
		pr_info("ok: cannot locate cr4 writing call gadget\n");
		return;
	}
	direct_write_cr4 = (void *)(insn + i);

	pr_info("trying to clear SMEP with call gadget\n");
	direct_write_cr4(cr4);
	if (native_read_cr4() & X86_CR4_SMEP) {
		pr_info("ok: SMEP removal was reverted\n");
	} else {
		pr_err("FAIL: cleared SMEP not detected!\n");
		cr4 |= X86_CR4_SMEP;
		pr_info("restoring SMEP\n");
		native_write_cr4(cr4);
	}
#else
	pr_err("XFAIL: this test is x86_64-only\n");
#endif
}

static void lkdtm_DOUBLE_FAULT(void)
{
#if IS_ENABLED(CONFIG_X86_32) && !IS_ENABLED(CONFIG_UML)
	/*
	 * Trigger #DF by setting the stack limit to zero.  This clobbers
	 * a GDT TLS slot, which is okay because the current task will die
	 * anyway due to the double fault.
	 */
	struct desc_struct d = {
		.type = 3,	/* expand-up, writable, accessed data */
		.p = 1,		/* present */
		.d = 1,		/* 32-bit */
		.g = 0,		/* limit in bytes */
		.s = 1,		/* not system */
	};

	local_irq_disable();
	write_gdt_entry(get_cpu_gdt_rw(smp_processor_id()),
			GDT_ENTRY_TLS_MIN, &d, DESCTYPE_S);

	/*
	 * Put our zero-limit segment in SS and then trigger a fault.  The
	 * 4-byte access to (%esp) will fault with #SS, and the attempt to
	 * deliver the fault will recursively cause #SS and result in #DF.
	 * This whole process happens while NMIs and MCEs are blocked by the
	 * MOV SS window.  This is nice because an NMI with an invalid SS
	 * would also double-fault, resulting in the NMI or MCE being lost.
	 */
	asm volatile ("movw %0, %%ss; addl $0, (%%esp)" ::
		      "r" ((unsigned short)(GDT_ENTRY_TLS_MIN << 3)));

	pr_err("FAIL: tried to double fault but didn't die\n");
#else
	pr_err("XFAIL: this test is ia32-only\n");
#endif
}

#ifdef CONFIG_ARM64
static noinline void change_pac_parameters(void)
{
	if (IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL)) {
		/* Reset the keys of current task */
		ptrauth_thread_init_kernel(current);
		ptrauth_thread_switch_kernel(current);
	}
}
#endif

static noinline void lkdtm_CORRUPT_PAC(void)
{
#ifdef CONFIG_ARM64
#define CORRUPT_PAC_ITERATE	10
	int i;

	if (!IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL))
		pr_err("FAIL: kernel not built with CONFIG_ARM64_PTR_AUTH_KERNEL\n");

	if (!system_supports_address_auth()) {
		pr_err("FAIL: CPU lacks pointer authentication feature\n");
		return;
	}

	pr_info("changing PAC parameters to force function return failure...\n");
	/*
	 * PAC is a hash value computed from input keys, return address and
	 * stack pointer. As pac has fewer bits so there is a chance of
	 * collision, so iterate few times to reduce the collision probability.
	 */
	for (i = 0; i < CORRUPT_PAC_ITERATE; i++)
		change_pac_parameters();

	pr_err("FAIL: survived PAC changes! Kernel may be unstable from here\n");
#else
	pr_err("XFAIL: this test is arm64-only\n");
#endif
}

static struct crashtype crashtypes[] = {
	CRASHTYPE(PANIC),
	CRASHTYPE(PANIC_STOP_IRQOFF),
	CRASHTYPE(BUG),
	CRASHTYPE(WARNING),
	CRASHTYPE(WARNING_MESSAGE),
	CRASHTYPE(EXCEPTION),
	CRASHTYPE(LOOP),
	CRASHTYPE(EXHAUST_STACK),
	CRASHTYPE(CORRUPT_STACK),
	CRASHTYPE(CORRUPT_STACK_STRONG),
	CRASHTYPE(REPORT_STACK),
	CRASHTYPE(REPORT_STACK_CANARY),
	CRASHTYPE(UNALIGNED_LOAD_STORE_WRITE),
	CRASHTYPE(SOFTLOCKUP),
	CRASHTYPE(HARDLOCKUP),
	CRASHTYPE(SPINLOCKUP),
	CRASHTYPE(HUNG_TASK),
	CRASHTYPE(OVERFLOW_SIGNED),
	CRASHTYPE(OVERFLOW_UNSIGNED),
	CRASHTYPE(ARRAY_BOUNDS),
	CRASHTYPE(FAM_BOUNDS),
	CRASHTYPE(CORRUPT_LIST_ADD),
	CRASHTYPE(CORRUPT_LIST_DEL),
	CRASHTYPE(STACK_GUARD_PAGE_LEADING),
	CRASHTYPE(STACK_GUARD_PAGE_TRAILING),
	CRASHTYPE(UNSET_SMEP),
	CRASHTYPE(DOUBLE_FAULT),
	CRASHTYPE(CORRUPT_PAC),
};

struct crashtype_category bugs_crashtypes = {
	.crashtypes = crashtypes,
	.len	    = ARRAY_SIZE(crashtypes),
};
