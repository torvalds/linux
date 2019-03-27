/* C9X struct/union member init, with nested union and trailing member */
union node {
	void *next;
	char *data;
};
struct foo {
	int b;
	union node n;
	int c;
};

struct foo f = {
	.b = 1,
	.n = { .next = 0, },
	.c = 1
};
