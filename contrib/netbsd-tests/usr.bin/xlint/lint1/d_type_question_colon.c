/* the type of the ?: expression should be the more specific type */

struct foo {
	int bar;
};

void
test(void) {
	int i;
	struct foo *ptr = 0;

	for (i = (ptr ? ptr : (void *)0)->bar; i < 10; i++)
		test();
}
