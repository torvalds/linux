/* PR 39639: writing "long double" gave "long int" */

int
fail(long double *a, long int *b)
{
	return a == b;
}
