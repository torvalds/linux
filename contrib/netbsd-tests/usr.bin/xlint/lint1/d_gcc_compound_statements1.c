/* GCC compound statements */

foo(unsigned long z)
{
	z = ({ unsigned long tmp; tmp = 1; tmp; });
	foo(z);
}
