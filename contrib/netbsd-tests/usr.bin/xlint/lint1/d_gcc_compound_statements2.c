/* GCC compound statements with non-expressions */
struct cpu_info {
	int bar;
};

int
main(void)
{
	return ({
	    struct cpu_info *__ci;
	    __asm__ volatile("movl %%fs:4,%0":"=r" (__ci));
	    __ci;
	})->bar;
}
