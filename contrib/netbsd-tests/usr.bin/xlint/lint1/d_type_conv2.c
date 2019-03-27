/* Flag information-losing type conversion in argument lists */

int f(float);

void
should_fail()
{
	double x = 2.0;

	f(x);
}
