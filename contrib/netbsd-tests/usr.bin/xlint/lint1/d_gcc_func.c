/* gcc __FUNCTION__ */

void
foo(const char *p) {
	p = __FUNCTION__;
	foo(p);
}
