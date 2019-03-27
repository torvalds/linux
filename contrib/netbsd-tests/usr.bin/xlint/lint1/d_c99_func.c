/* C99 __func__ */

void
foo(const char *p) {
	p = __func__;
	foo(p);
}
