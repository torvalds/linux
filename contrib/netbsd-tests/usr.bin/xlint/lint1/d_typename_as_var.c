typedef char h[10];

typedef struct {
	int i;
	char *c;
} fh;

struct foo {
	fh h;
	struct {
		int x;
		int y;
	} fl;
};
