/* extension */
void a(void) {
    double __logbw = 1;
    if (__extension__(({ __typeof((__logbw)) x_ = (__logbw); !__builtin_isinf((x_)) && !__builtin_isnan((x_)); })))
	__logbw = 1;
}
