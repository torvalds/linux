/* compound literals */

struct p {
	short a, b, c, d;
} zz = {
	1, 2, 3, 4
};

struct p *bar(int i)
{
	static struct p q[10];
	return &q[i];
}

foo()
{
	*bar(1) = (struct p) { 1, 2, 3, 4 };
}
