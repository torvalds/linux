/* Flag information-losing type conversion in argument lists */

int f(unsigned int);

void
should_fail()
{
	long long x = 20;

	f(x);
}
