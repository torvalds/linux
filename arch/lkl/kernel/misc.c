#include <linux/seq_file.h>

// TODO: Functions __generic_xchg_called_with_bad_pointer and wrong_size_cmpxchg
// are called in __generic_xchg and __generic_cmpxchg_local respectively.
// They should be optimized out by the compiler due to the function
// inlining. However, when building with clang there are some instances
// where the functions aren't inlined and, thus, the compile-time optimization
// doesn't eliminate them entirely. As a result, the linker throws
// unresololved symbols error. As a workaround, the fix below define these
// functions to bypass the link-time error.
void __generic_xchg_called_with_bad_pointer(void)
{
	panic("%s shouldn't be executed\n", __func__);
}
unsigned long wrong_size_cmpxchg(volatile void *ptr)
{
	panic("%s shouldn't be executed\n", __func__);
}

#ifdef CONFIG_PROC_FS
static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= cpuinfo_start,
	.next	= cpuinfo_next,
	.stop	= cpuinfo_stop,
	.show	= show_cpuinfo,
};
#endif
