/*****************************************************************************
 *
 *  libntpq.c
 *
 *  This is the wrapper library for ntpq, the NTP query utility. 
 *  This library reuses the sourcecode from ntpq and exports a number
 *  of useful functions in a library that can be linked against applications
 *  that need to query the status of a running ntpd. The whole 
 *  communcation is based on mode 6 packets.
 *
 ****************************************************************************/
#define LIBNTPQ_C
#define NO_MAIN_ALLOWED 1
/* #define BUILD_AS_LIB		Already provided by the Makefile */

#include "ntpq.c"
#include "libntpq.h"

/* Function Prototypes */
 

const char *Version = "libntpq 0.3beta";

/* global variables used for holding snapshots of data */
char peervars[NTPQ_BUFLEN];
int peervarlen = 0;
associd_t peervar_assoc = 0;
char clockvars[NTPQ_BUFLEN];
int clockvarlen = 0;
int clockvar_assoc = 0;
char sysvars[NTPQ_BUFLEN];
int sysvarlen = 0;
char *ntpq_resultbuffer[NTPQ_BUFLEN];
unsigned short ntpq_associations[MAXASSOC];
struct ntpq_varlist ntpq_varlist[MAXLIST];

/*****************************************************************************
 *
 *  ntpq_stripquotes
 *
 *  Parses a given character buffer srcbuf and removes all quoted
 *  characters. The resulting string is copied to the specified 
 *  resultbuf character buffer.  E.g. \" will be translated into "
 * 
 ****************************************************************************
 * Parameters:
 *	resultbuf	char*	The resulting string without quoted
 *				characters
 *	srcbuf		char*	The buffer holding the original string
 *	datalen		int	The number of bytes stored in srcbuf
 *	maxlen		int	Max. number of bytes for resultbuf
 *
 * Returns:
 *	int		number of chars that have been copied to 
 *			resultbuf 
 ****************************************************************************/

int ntpq_stripquotes ( char *resultbuf, char *srcbuf, int datalen, int maxlen )
{
	char* dst = resultbuf;
	char* dep = resultbuf + maxlen - 1;
	char* src = srcbuf;
	char* sep = srcbuf + (datalen >= 0 ? datalen : 0); 
	int   esc = 0;
	int   ch;
	
	if (maxlen <= 0)
		return 0;
	
	while ((dst != dep) && (src != sep) && (ch = (u_char)*src++) != 0) {
		if (esc) {
			esc = 0;
			switch (ch) {
				/* skip and do not copy */
				/* case '"':*/ /* quotes */
			case 'n': /*newline*/
			case 'r': /*carriage return*/
			case 'g': /*bell*/
			case 't': /*tab*/
				continue;
			default:
				break;
			}
		} else {
			switch (ch) {
			case '\\':
				esc = 1;
			case '"':
				continue;
			default:
				break;
			}
		}
		*dst++ = (char)ch;
	}
	*dst = '\0';
	return (int)(dst - resultbuf);
}			


/*****************************************************************************
 *
 *  ntpq_getvar
 *
 *  This function parses a given buffer for a variable/value pair and
 *  copies the value of the requested variable into the specified
 *  varvalue buffer.
 *
 *  It returns the number of bytes copied or zero for an empty result
 *  (=no matching variable found or empty value)
 *
 ****************************************************************************
 * Parameters:
 *	resultbuf	char*	The resulting string without quoted
 *				characters
 *	datalen		size_t	The number of bytes stored in 
 *							resultbuf
 *	varname		char*	Name of the required variable 
 *	varvalue	char*	Where the value of the variable should
 *							be stored
 *	maxlen		size_t	Max. number of bytes for varvalue
 *
 * Returns:
 *	size_t		number of chars that have been copied to 
 *			varvalue
 ****************************************************************************/

size_t
ntpq_getvar(
	const char *	resultbuf,
	size_t		datalen,
	const char *	varname,
	char *		varvalue,
	size_t		maxlen)
{
	char *	name;
	char *	value;
	size_t	idatalen;

	value = NULL;
	idatalen = (int)datalen;

	while (nextvar(&idatalen, &resultbuf, &name, &value)) {
		if (strcmp(varname, name) == 0) {
			ntpq_stripquotes(varvalue, value, strlen(value), maxlen);

			return strlen(varvalue);
		}
	}

	return 0;
}


/*****************************************************************************
 *
 *  ntpq_queryhost
 *
 *  Sends a mode 6 query packet to the current open host (see 
 *  ntpq_openhost) and stores the requested variable set in the specified
 *  character buffer. 
 *  It returns the number of bytes read or zero for an empty result
 *  (=no answer or empty value)
 *
 ****************************************************************************
 * Parameters:
 *      VARSET		u_short	Which variable set should be
 *				read (PEERVARS or CLOCKVARS)
 *	association	int	The association ID that should be read
 *				0 represents the ntpd instance itself
 *	resultbuf	char*	The resulting string without quoted
 *				characters
 *	maxlen		int	Max. number of bytes for varvalue
 *
 * Returns:
 *	int		number of bytes that have been copied to 
 *			resultbuf
 *  			- OR -
 *			0 (zero) if no reply has been received or
 *			another failure occured
 ****************************************************************************/

int ntpq_queryhost(unsigned short VARSET, unsigned short association, char *resultbuf, int maxlen)
{
	const char *datap;
	int res;
	size_t	dsize;
	u_short	rstatus;
	
	if ( numhosts > 0 )
		res = doquery(VARSET,association,0,0, (char *)0, &rstatus, &dsize, &datap);
	else
		return 0;
	
	if ( ( res != 0) || ( dsize == 0 ) ) /* no data */
		return 0;
	
	if ( dsize > maxlen) 
		dsize = maxlen;
	
	
	/* fill result resultbuf */
	memcpy(resultbuf, datap, dsize);
	
	return dsize;
}



/*****************************************************************************
 *
 *  ntpq_openhost
 *
 *  Sets up a connection to the ntpd instance of a specified host. Note:
 *  There is no real "connection" established because NTP solely works
 *  based on UDP.
 *
 ****************************************************************************
 * Parameters:
 *	hostname	char*	Hostname/IP of the host running ntpd
 *	fam		int	Address Family (AF_INET, AF_INET6, or 0)
 *
 * Returns:
 *	int		1 if the host connection could be set up, i.e. 
 *			name resolution was succesful and/or IP address
 *			has been validated
 *  			- OR -
 *			0 (zero) if a failure occured
 ****************************************************************************/

int
ntpq_openhost(
	char *hostname,
	int fam
	)
{
	if ( openhost(hostname, fam) )
	{
		numhosts = 1;
	} else {
		numhosts = 0;
	}
	
	return numhosts;
	
}


/*****************************************************************************
 *
 *  ntpq_closehost
 *
 *  Cleans up a connection by closing the used socket. Should be called
 *  when no further queries are required for the currently used host.
 *
 ****************************************************************************
 * Parameters:
 *	- none -
 *
 * Returns:
 *	int		0 (zero) if no host has been opened before
 *			- OR -
 *			the resultcode from the closesocket function call
 ****************************************************************************/

int ntpq_closehost(void)
{
	if ( numhosts )
	 return closesocket(sockfd);
	
	return 0;
}


/*****************************************************************************
 *
 *  ntpq_read_associations
 *
 *  This function queries the ntp host for its associations and returns the 
 *  number of associations found.
 *
 *  It takes an u_short array as its first parameter, this array holds the 
 *  IDs of the associations, 
 *  the function will not write more entries than specified with the 
 *  max_entries parameter.
 *
 *  However, if more than max_entries associations were found, the return 
 *  value of this function will reflect the real number, even if not all 
 *  associations have been stored in the array.
 *
 ****************************************************************************
 * Parameters:
 *	resultbuf	u_short*Array that should hold the list of
 *				association IDs
 *	maxentries	int	maximum number of association IDs that can
 *				be stored in resultbuf
 *
 * Returns:
 *	int		number of association IDs stored in resultbuf
 *  			- OR -
 *			0 (zero) if a failure occured or no association has
 *			been returned.
 ****************************************************************************/
 
 int  ntpq_read_associations ( u_short resultbuf[], int max_entries )
{
    int i = 0;

    if (ntpq_dogetassoc()) {       
        
        if(numassoc < max_entries)
          max_entries = numassoc;

        for (i=0;i<max_entries;i++)
            resultbuf[i] = assoc_cache[i].assid;

        return numassoc;
    }

    return 0;
}




/*****************************************************************************
 *
 *  ntpq_get_assocs
 *
 *  This function reads the associations of a previously selected (with 
 *  ntpq_openhost) NTP host into its own (global) array and returns the 
 *  number of associations found. 
 *
 *  The obtained association IDs can be read by using the ntpq_get_assoc_id 
 *  function.
 *
 ****************************************************************************
 * Parameters:
 *	- none -
 *
 * Returns:
 *	int		number of association IDs stored in resultbuf
 *  			- OR -
 *			0 (zero) if a failure occured or no association has
 *			been returned.
 ****************************************************************************/
 
 int  ntpq_get_assocs ( void )
{
    return ntpq_read_associations( ntpq_associations, MAXASSOC );
}


/*****************************************************************************
 *  
 *  ntpq_get_assoc_number
 *
 *  This function returns for a given Association ID the association number 
 *  in the internal association array, which is filled by the ntpq_get_assocs 
 *  function.
 * 
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *
 * Returns:
 *	int		the number of the association array element that is
 *			representing the given association ID
 *  			- OR -
 *			-1 if a failure occured or no matching association 
 * 			ID has been found
 ****************************************************************************/
 
int ntpq_get_assoc_number ( associd_t associd )
{
	int i;

	for (i=0;i<numassoc;i++) {
		if (assoc_cache[i].assid == associd)
			return i;
	}

	return -1;

}


/*****************************************************************************
 *  
 *  ntpq_read_assoc_peervars
 *
 *  This function reads the peervars variable-set of a specified association 
 *  from a NTP host and writes it to the result buffer specified, honoring 
 *  the maxsize limit.
 *
 *  It returns the number of bytes written or 0 when the variable-set is 
 *  empty or failed to read.
 *  
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *	resultbuf	char*	character buffer where the variable set
 *				should be stored
 *	maxsize		int	the maximum number of bytes that can be
 *				written to resultbuf
 *
 * Returns:
 *	int		number of chars that have been copied to 
 *			resultbuf
 *			- OR - 
 *			0 (zero) if an error occured
 ****************************************************************************/

int
ntpq_read_assoc_peervars(
	associd_t	associd,
	char *		resultbuf,
	int		maxsize
	)
{
	const char *	datap;
	int		res;
	size_t		dsize;
	u_short		rstatus;

	res = doquery(CTL_OP_READVAR, associd, 0, 0, NULL, &rstatus,
		      &dsize, &datap);
	if (res != 0)
		return 0;
	if (dsize <= 0) {
		if (numhosts > 1)
			fprintf(stderr, "server=%s ", currenthost);
		fprintf(stderr,
			"***No information returned for association %d\n",
			associd);

		return 0;
	}
	if (dsize > maxsize) 
		dsize = maxsize;
	memcpy(resultbuf, datap, dsize);

	return dsize;
}




/*****************************************************************************
 *  
 *  ntpq_read_sysvars
 *
 *  This function reads the sysvars variable-set from a NTP host and writes it
 *  to the result buffer specified, honoring the maxsize limit.
 *
 *  It returns the number of bytes written or 0 when the variable-set is empty
 *  or could not be read.
 *  
 ****************************************************************************
 * Parameters:
 *	resultbuf	char*	character buffer where the variable set
 *				should be stored
 *	maxsize		int	the maximum number of bytes that can be
 *				written to resultbuf
 *
 * Returns:
 *	int		number of chars that have been copied to 
 *			resultbuf
 *			- OR - 
 *			0 (zero) if an error occured
 ****************************************************************************/
size_t
ntpq_read_sysvars(
	char *	resultbuf,
	size_t	maxsize
	)
{
	const char *	datap;
	int		res;
	size_t		dsize;
	u_short		rstatus;

	res = doquery(CTL_OP_READVAR, 0, 0, 0, NULL, &rstatus,
		      &dsize, &datap);

	if (res != 0)
		return 0;

	if (dsize == 0) {
		if (numhosts > 1)
			fprintf(stderr, "server=%s ", currenthost);
		fprintf(stderr, "***No sysvar information returned\n");

		return 0;
	} else {
		dsize = min(dsize, maxsize);
		memcpy(resultbuf, datap, dsize);
	}

	return dsize;
}


/*****************************************************************************
 *  ntpq_get_assoc_allvars
 *
 *  With this function all association variables for the specified association
 *  ID can be requested from a NTP host. They are stored internally and can be
 *  read by using the ntpq_get_peervar or ntpq_get_clockvar functions.
 *
 *  Basically this is only a combination of the ntpq_get_assoc_peervars and 
 *  ntpq_get_assoc_clockvars functions.
 *
 *  It returns 1 if both variable-sets (peervars and clockvars) were 
 *  received successfully. If one variable-set or both of them weren't 
 *  received,
 *
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *
 * Returns:
 *	int		nonzero if at least one variable set could be read
 * 			- OR - 
 *			0 (zero) if an error occured and both variable sets
 *			could not be read
 ****************************************************************************/
 int  ntpq_get_assoc_allvars( associd_t associd  )
{
	return ntpq_get_assoc_peervars ( associd ) &
	       ntpq_get_assoc_clockvars( associd );
}




/*****************************************************************************
 *
 *  ntpq_get_sysvars
 *
 *  The system variables of a NTP host can be requested by using this function
 *  and afterwards using ntpq_get_sysvar to read the single variable values.
 *
 ****************************************************************************
 * Parameters:
 *	- none -
 *
 * Returns:
 *	int		nonzero if the variable set could be read
 * 			- OR - 
 *			0 (zero) if an error occured and the sysvars
 *			could not be read
 ****************************************************************************/
int
ntpq_get_sysvars(void)
{
	sysvarlen = ntpq_read_sysvars(sysvars, sizeof(sysvars));
	if (sysvarlen <= 0)
		return 0;
	else
		return 1;
}


/*****************************************************************************
 *  
 *  ntp_get_peervar
 *
 *  This function uses the variable-set which was read by using 
 *  ntp_get_peervars and searches for a variable specified with varname. If 
 *  such a variable exists, it writes its value into
 *  varvalue (maxlen specifies the size of this target buffer).
 *  
 ****************************************************************************
 * Parameters:
 *	varname		char*	requested variable name
 *	varvalue	char*	the buffer where the value should go into
 *	maxlen		int	maximum number of bytes that can be copied to
 *				varvalue
 *
 * Returns:
 *	int		number of bytes copied to varvalue
 * 			- OR - 
 *			0 (zero) if an error occured or the variable could 
 *			not be found
 ****************************************************************************/
int ntpq_get_peervar( const char *varname, char *varvalue, int maxlen)
{
    return ( ntpq_getvar(peervars,peervarlen,varname,varvalue,maxlen) );
}



/*****************************************************************************
 *  
 *  ntpq_get_assoc_peervars
 *
 *  This function requests the peer variables of the specified association 
 *  from a NTP host. In order to access the variable values, the function 
 *  ntpq_get_peervar must be used.
 *
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *
 * Returns:
 *	int		1 (one) if the peervars have been read
 * 			- OR - 
 *			0 (zero) if an error occured and the variable set
 *			could not be read
 ****************************************************************************/
int
ntpq_get_assoc_peervars(
	associd_t associd
	)
{
	peervarlen = ntpq_read_assoc_peervars(associd, peervars, 
					      sizeof(peervars));
	if (peervarlen <= 0) {
		peervar_assoc = 0;

		return 0;
	}
	peervar_assoc = associd;

	return 1;
}


/*****************************************************************************
 *  
 *  ntp_read_assoc_clockvars
 *
 *  This function reads the clockvars variable-set of a specified association
 *  from a NTP host and writes it to the result buffer specified, honoring 
 *  the maxsize limit.
 *
 *  It returns the number of bytes written or 0 when the variable-set is 
 *  empty or failed to read.
 *  
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *	resultbuf	char*	character buffer where the variable set
 *				should be stored
 *	maxsize		int	the maximum number of bytes that can be
 *				written to resultbuf
 *
 * Returns:
 *	int		number of chars that have been copied to 
 *			resultbuf
 *			- OR - 
 *			0 (zero) if an error occured
 ****************************************************************************/

int
ntpq_read_assoc_clockvars(
	associd_t	associd,
	char *		resultbuf,
	int		maxsize
	)
{
	const char *datap;
	int res;
	size_t dsize;
	u_short rstatus;

	res = ntpq_doquerylist(ntpq_varlist, CTL_OP_READCLOCK, associd,
			       0, &rstatus, &dsize, &datap);
	if (res != 0)
		return 0;

	if (dsize == 0) {
		if (numhosts > 1) /* no information returned from server */
			return 0;
	} else {
		if (dsize > maxsize) 
			dsize = maxsize;
		memcpy(resultbuf, datap, dsize);
	}

	return dsize;
}



/*****************************************************************************
 *  
 *  ntpq_get_assoc_clocktype
 *
 *  This function returns a clocktype value for a given association number 
 *  (not ID!):
 *
 *  NTP_CLOCKTYPE_UNKNOWN   Unknown clock type
 *  NTP_CLOCKTYPE_BROADCAST Broadcast server
 *  NTP_CLOCKTYPE_LOCAL     Local clock
 *  NTP_CLOCKTYPE_UNICAST   Unicast server
 *  NTP_CLOCKTYPE_MULTICAST Multicast server
 * 
 ****************************************************************************/
int
ntpq_get_assoc_clocktype(
	int assoc_index
	)
{
	associd_t	associd;
	int		i;
	int		rc;
	sockaddr_u	dum_store;
	char		dstadr[LENHOSTNAME];
	char		resultbuf[NTPQ_BUFLEN];

	if (assoc_index < 0 || assoc_index >= numassoc)
		return -1;

	associd = assoc_cache[assoc_index].assid;
	if (associd == peervar_assoc) {
		rc = ntpq_get_peervar("dstadr", dstadr, sizeof(dstadr));
	} else {
		i = ntpq_read_assoc_peervars(associd, resultbuf,
					     sizeof(resultbuf));
		if (i <= 0)
			return -1;
		rc = ntpq_getvar(resultbuf, i, "dstadr", dstadr,
				 sizeof(dstadr));
	}

	if (0 != rc && decodenetnum(dstadr, &dum_store))
		return ntpq_decodeaddrtype(&dum_store);

	return -1;
}



/*****************************************************************************
 *  
 *  ntpq_get_assoc_clockvars
 *
 *  With this function the clock variables of the specified association are 
 *  requested from a NTP host. This makes only sense for associations with 
 *  the type 'l' (Local Clock) and you should check this with 
 *  ntpq_get_assoc_clocktype for each association, before you use this function
 *  on it.
 *
 ****************************************************************************
 * Parameters:
 *	associd		int	requested associaton ID 
 *
 * Returns:
 *	int		1 (one) if the clockvars have been read
 * 			- OR - 
 *			0 (zero) if an error occured and the variable set
 *			could not be read
 ****************************************************************************/
int  ntpq_get_assoc_clockvars( associd_t associd )
{
	if (NTP_CLOCKTYPE_LOCAL != ntpq_get_assoc_clocktype(
	    ntpq_get_assoc_number(associd)))
		return 0;
	clockvarlen = ntpq_read_assoc_clockvars( associd, clockvars,
						 sizeof(clockvars) );
	if ( clockvarlen <= 0 ) {
		clockvar_assoc = 0;
		return 0;
	} else {
		clockvar_assoc = associd;
		return 1;
	}
}


