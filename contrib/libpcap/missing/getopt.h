/*
 * Header for the getopt() we supply if the platform doesn't supply it.
 */
extern char *optarg;			/* getopt(3) external variables */
extern int optind, opterr, optreset, optopt;

extern int getopt(int nargc, char * const *nargv, const char *ostr);
