/* Flag information-losing constant conversion in argument lists */

int f(unsigned int);

void
should_fail()
{
	f(2.1);
}
