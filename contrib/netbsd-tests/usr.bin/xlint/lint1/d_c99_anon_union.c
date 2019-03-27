/* struct with only anonymous members */

struct foo {
	union {
		long loo;
		double doo;
	};
};

int
main(void) {

	struct foo *f = 0;
	printf("%p\n", &f[1]);
	return 0;
}
