/* GCC compound statements with void type */

void
main(void)
{
	({
		void *v;
		__asm__ volatile("noop");
	});
}
