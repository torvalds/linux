/* CVT node handling in ?: operator */
typedef unsigned long int size_t;
struct filecore_direntry {
	unsigned len:32;
};
int
main(void)
{
	struct filecore_direntry dirent = { 0 };
	size_t  uio_resid = 0;
	size_t bytelen = (((dirent.len)<(uio_resid))?(dirent.len):(uio_resid));
	return bytelen;
}
