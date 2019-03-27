/* Flag information-losing type conversion in argument lists */

int f(unsigned int);

void
should_fail()
{

	f(0x7fffffffffffffffLL);
}
