/* union cast */

struct bar {
	int a;
	int b;
};

union foo {
	struct bar *a;
	int b;
};

void
foo(void) {
	struct bar *a;

	((union foo)a).a;
}
