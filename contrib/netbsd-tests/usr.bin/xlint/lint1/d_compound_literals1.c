/* compound literals */

struct p {
	short a, b, c, d;
};

foo()
{
	struct p me = (struct p) {1, 2, 3, 4};
	me.a = me.b;
}
