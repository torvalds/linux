/* gcc: variable array initializer */
void foo(int i)
{
	int array[i];
	while (i--)
		foo(array[i] = 0);
}
