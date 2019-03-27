#include <sandbox.h>

int
main(void)
{
	char	*ep;
	int	 rc;

	rc = sandbox_init(kSBXProfileNoInternet, SANDBOX_NAMED, &ep);
	if (-1 == rc)
		sandbox_free_error(ep);
	return(-1 == rc);
}
