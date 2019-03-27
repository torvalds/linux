#define LIBNAME "pthread"
#include "sem.c"

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, postwait);
	ATF_TP_ADD_TC(tp, initvalue);
	ATF_TP_ADD_TC(tp, destroy);
	ATF_TP_ADD_TC(tp, busydestroy);
	ATF_TP_ADD_TC(tp, blockwait);
	ATF_TP_ADD_TC(tp, blocktimedwait);
	ATF_TP_ADD_TC(tp, named);
	ATF_TP_ADD_TC(tp, unlink);

	return atf_no_error();
}
