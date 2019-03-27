/* Anonymous struct test */

typedef int type;

struct point {
	int x;
	int y;
};

struct bar {
	struct {
		struct point top_left;
		struct point bottom_right;
	};
	type z;
};


int
main(void)
{
	struct bar b;
	b.top_left.x = 1;
	return 0;
}
	
