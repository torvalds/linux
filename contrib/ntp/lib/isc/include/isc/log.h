/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: log.h,v 1.59 2009/02/16 02:01:16 marka Exp $ */

#ifndef ISC_LOG_H
#define ISC_LOG_H 1

/*! \file isc/log.h */

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h> /* XXXDCL NT */

#include <isc/formatcheck.h>
#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

/*@{*/
/*!
 * \brief Severity levels, patterned after Unix's syslog levels.
 *
 */
#define ISC_LOG_DEBUG(level)	(level)
/*!
 * #ISC_LOG_DYNAMIC can only be used for defining channels with
 * isc_log_createchannel(), not to specify a level in isc_log_write().
 */
#define ISC_LOG_DYNAMIC	  	  0
#define ISC_LOG_INFO		(-1)
#define ISC_LOG_NOTICE		(-2)
#define ISC_LOG_WARNING 	(-3)
#define ISC_LOG_ERROR		(-4)
#define ISC_LOG_CRITICAL	(-5)
/*@}*/

/*@{*/
/*!
 * \brief Destinations.
 */
#define ISC_LOG_TONULL		1
#define ISC_LOG_TOSYSLOG	2
#define ISC_LOG_TOFILE		3
#define ISC_LOG_TOFILEDESC	4
/*@}*/

/*@{*/
/*%
 * Channel flags.
 */
#define ISC_LOG_PRINTTIME	0x0001
#define ISC_LOG_PRINTLEVEL	0x0002
#define ISC_LOG_PRINTCATEGORY	0x0004
#define ISC_LOG_PRINTMODULE	0x0008
#define ISC_LOG_PRINTTAG	0x0010
#define ISC_LOG_PRINTALL	0x001F
#define ISC_LOG_DEBUGONLY	0x1000
#define ISC_LOG_OPENERR		0x8000		/* internal */
/*@}*/

/*@{*/
/*!
 * \brief Other options.
 *
 * XXXDCL INFINITE doesn't yet work.  Arguably it isn't needed, but
 *   since I am intend to make large number of versions work efficiently,
 *   INFINITE is going to be trivial to add to that.
 */
#define ISC_LOG_ROLLINFINITE	(-1)
#define ISC_LOG_ROLLNEVER	(-2)
/*@}*/

/*!
 * \brief Used to name the categories used by a library.
 *
 * An array of isc_logcategory
 * structures names each category, and the id value is initialized by calling
 * isc_log_registercategories.
 */
struct isc_logcategory {
	const char *name;
	unsigned int id;
};

/*%
 * Similar to isc_logcategory, but for all the modules a library defines.
 */
struct isc_logmodule {
	const char *name;
	unsigned int id;
};

/*%
 * The isc_logfile structure is initialized as part of an isc_logdestination
 * before calling isc_log_createchannel().
 *
 * When defining an #ISC_LOG_TOFILE
 * channel the name, versions and maximum_size should be set before calling
 * isc_log_createchannel().  To define an #ISC_LOG_TOFILEDESC channel set only
 * the stream before the call.
 *
 * Setting maximum_size to zero implies no maximum.
 */
typedef struct isc_logfile {
	FILE *stream;		/*%< Initialized to NULL for #ISC_LOG_TOFILE. */
	const char *name;	/*%< NULL for #ISC_LOG_TOFILEDESC. */
	int versions;	/* >= 0, #ISC_LOG_ROLLNEVER, #ISC_LOG_ROLLINFINITE. */
	/*%
	 * stdio's ftell is standardized to return a long, which may well not
	 * be big enough for the largest file supportable by the operating
	 * system (though it is _probably_ big enough for the largest log
	 * anyone would want).  st_size returned by fstat should be typedef'd
	 * to a size large enough for the largest possible file on a system.
	 */
	isc_offset_t maximum_size;
	isc_boolean_t maximum_reached; /*%< Private. */
} isc_logfile_t;

/*%
 * Passed to isc_log_createchannel to define the attributes of either
 * a stdio or a syslog log.
 */
typedef union isc_logdestination {
	isc_logfile_t file;
	int facility;		/* XXXDCL NT */
} isc_logdestination_t;

/*@{*/
/*%
 * The built-in categories of libisc.
 *
 * Each library registering categories should provide library_LOGCATEGORY_name
 * definitions with indexes into its isc_logcategory structure corresponding to
 * the order of the names.
 */
LIBISC_EXTERNAL_DATA extern isc_logcategory_t isc_categories[];
LIBISC_EXTERNAL_DATA extern isc_log_t *isc_lctx;
LIBISC_EXTERNAL_DATA extern isc_logmodule_t isc_modules[];
/*@}*/

/*@{*/
/*%
 * Do not log directly to DEFAULT.  Use another category.  When in doubt,
 * use GENERAL.
 */
#define ISC_LOGCATEGORY_DEFAULT	(&isc_categories[0])
#define ISC_LOGCATEGORY_GENERAL	(&isc_categories[1])
/*@}*/

#define ISC_LOGMODULE_SOCKET (&isc_modules[0])
#define ISC_LOGMODULE_TIME (&isc_modules[1])
#define ISC_LOGMODULE_INTERFACE (&isc_modules[2])
#define ISC_LOGMODULE_TIMER (&isc_modules[3])
#define ISC_LOGMODULE_FILE (&isc_modules[4])

ISC_LANG_BEGINDECLS

isc_result_t
isc_log_create(isc_mem_t *mctx, isc_log_t **lctxp, isc_logconfig_t **lcfgp);
/*%<
 * Establish a new logging context, with default channels.
 *
 * Notes:
 *\li	isc_log_create() calls isc_logconfig_create(), so see its comment
 *	below for more information.
 *
 * Requires:
 *\li	mctx is a valid memory context.
 *\li	lctxp is not null and *lctxp is null.
 *\li	lcfgp is null or lcfgp is not null and *lcfgp is null.
 *
 * Ensures:
 *\li	*lctxp will point to a valid logging context if all of the necessary
 *	memory was allocated, or NULL otherwise.
 *\li	*lcfgp will point to a valid logging configuration if all of the
 *	necessary memory was allocated, or NULL otherwise.
 *\li	On failure, no additional memory is allocated.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		Success
 *\li	#ISC_R_NOMEMORY		Resource limit: Out of memory
 */

isc_result_t
isc_logconfig_create(isc_log_t *lctx, isc_logconfig_t **lcfgp);
/*%<
 * Create the data structure that holds all of the configurable information
 * about where messages are actually supposed to be sent -- the information
 * that could changed based on some configuration file, as opposed to the
 * the category/module specification of isc_log_[v]write[1] that is compiled
 * into a program, or the debug_level which is dynamic state information.
 *
 * Notes:
 *\li	It is necessary to specify the logging context the configuration
 * 	will be used with because the number of categories and modules
 *	needs to be known in order to set the configuration.  However,
 *	the configuration is not used by the logging context until the
 *	isc_logconfig_use function is called.
 *
 *\li	The memory context used for operations that allocate memory for
 *	the configuration is that of the logging context, as specified
 *	in the isc_log_create call.
 *
 *\li	Four default channels are established:
 *\verbatim
 *	    	default_syslog
 *		 - log to syslog's daemon facility #ISC_LOG_INFO or higher
 *		default_stderr
 *		 - log to stderr #ISC_LOG_INFO or higher
 *		default_debug
 *		 - log to stderr #ISC_LOG_DEBUG dynamically
 *		null
 *		 - log nothing
 *\endverbatim
 *
 * Requires:
 *\li 	lctx is a valid logging context.
 *\li	lcftp is not null and *lcfgp is null.
 *
 * Ensures:
 *\li	*lcfgp will point to a valid logging context if all of the necessary
 *	memory was allocated, or NULL otherwise.
 *\li	On failure, no additional memory is allocated.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		Success
 *\li	#ISC_R_NOMEMORY		Resource limit: Out of memory
 */

isc_logconfig_t *
isc_logconfig_get(isc_log_t *lctx);
/*%<
 * Returns a pointer to the configuration currently in use by the log context.
 *
 * Requires:
 *\li	lctx is a valid context.
 *
 * Ensures:
 *\li	The configuration pointer is non-null.
 *
 * Returns:
 *\li	The configuration pointer.
 */

isc_result_t
isc_logconfig_use(isc_log_t *lctx, isc_logconfig_t *lcfg);
/*%<
 * Associate a new configuration with a logging context.
 *
 * Notes:
 *\li	This is thread safe.  The logging context will lock a mutex
 *	before attempting to swap in the new configuration, and isc_log_doit
 *	(the internal function used by all of isc_log_[v]write[1]) locks
 *	the same lock for the duration of its use of the configuration.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *\li	lcfg is a valid logging configuration.
 *\li	lctx is the same configuration given to isc_logconfig_create
 *		when the configuration was created.
 *
 * Ensures:
 *\li	Future calls to isc_log_write will use the new configuration.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		Success
 *\li	#ISC_R_NOMEMORY		Resource limit: Out of memory
 */

void
isc_log_destroy(isc_log_t **lctxp);
/*%<
 * Deallocate the memory associated with a logging context.
 *
 * Requires:
 *\li	*lctx is a valid logging context.
 *
 * Ensures:
 *\li	All of the memory associated with the logging context is returned
 *	to the free memory pool.
 *
 *\li	Any open files are closed.
 *
 *\li	The logging context is marked as invalid.
 */

void
isc_logconfig_destroy(isc_logconfig_t **lcfgp);
/*%<
 * Destroy a logging configuration.
 *
 * Notes:
 *\li	This function cannot be used directly with the return value of
 *	isc_logconfig_get, because a logging context must always have
 *	a valid configuration associated with it.
 *
 * Requires:
 *\li	lcfgp is not null and *lcfgp is a valid logging configuration.
 *\li	The logging configuration is not in use by an existing logging context.
 *
 * Ensures:
 *\li	All memory allocated for the configuration is freed.
 *
 *\li	The configuration is marked as invalid.
 */

void
isc_log_registercategories(isc_log_t *lctx, isc_logcategory_t categories[]);
/*%<
 * Identify logging categories a library will use.
 *
 * Notes:
 *\li	A category should only be registered once, but no mechanism enforces
 *	this rule.
 *
 *\li	The end of the categories array is identified by a NULL name.
 *
 *\li	Because the name is used by #ISC_LOG_PRINTCATEGORY, it should not
 *	be altered or destroyed after isc_log_registercategories().
 *
 *\li	Because each element of the categories array is used by
 *	isc_log_categorybyname, it should not be altered or destroyed
 *	after registration.
 *
 *\li	The value of the id integer in each structure is overwritten
 *	by this function, and so id need not be initialized to any particular
 *	value prior to the function call.
 *
 *\li	A subsequent call to isc_log_registercategories with the same
 *	logging context (but new categories) will cause the last
 *	element of the categories array from the prior call to have
 *	its "name" member changed from NULL to point to the new
 *	categories array, and its "id" member set to UINT_MAX.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *\li	categories != NULL.
 *\li	categories[0].name != NULL.
 *
 * Ensures:
 * \li	There are references to each category in the logging context,
 * 	so they can be used with isc_log_usechannel() and isc_log_write().
 */

void
isc_log_registermodules(isc_log_t *lctx, isc_logmodule_t modules[]);
/*%<
 * Identify logging categories a library will use.
 *
 * Notes:
 *\li	A module should only be registered once, but no mechanism enforces
 *	this rule.
 *
 *\li	The end of the modules array is identified by a NULL name.
 *
 *\li	Because the name is used by #ISC_LOG_PRINTMODULE, it should not
 *	be altered or destroyed after isc_log_registermodules().
 *
 *\li	Because each element of the modules array is used by
 *	isc_log_modulebyname, it should not be altered or destroyed
 *	after registration.
 *
 *\li	The value of the id integer in each structure is overwritten
 *	by this function, and so id need not be initialized to any particular
 *	value prior to the function call.
 *
 *\li	A subsequent call to isc_log_registermodules with the same
 *	logging context (but new modules) will cause the last
 *	element of the modules array from the prior call to have
 *	its "name" member changed from NULL to point to the new
 *	modules array, and its "id" member set to UINT_MAX.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *\li	modules != NULL.
 *\li	modules[0].name != NULL;
 *
 * Ensures:
 *\li	Each module has a reference in the logging context, so they can be
 *	used with isc_log_usechannel() and isc_log_write().
 */

isc_result_t
isc_log_createchannel(isc_logconfig_t *lcfg, const char *name,
		      unsigned int type, int level,
		      const isc_logdestination_t *destination,
		      unsigned int flags);
/*%<
 * Specify the parameters of a logging channel.
 *
 * Notes:
 *\li	The name argument is copied to memory in the logging context, so
 *	it can be altered or destroyed after isc_log_createchannel().
 *
 *\li	Defining a very large number of channels will have a performance
 *	impact on isc_log_usechannel(), since the names are searched
 *	linearly until a match is made.  This same issue does not affect
 *	isc_log_write, however.
 *
 *\li	Channel names can be redefined; this is primarily useful for programs
 *	that want their own definition of default_syslog, default_debug
 *	and default_stderr.
 *
 *\li	Any channel that is redefined will not affect logging that was
 *	already directed to its original definition, _except_ for the
 *	default_stderr channel.  This case is handled specially so that
 *	the default logging category can be changed by redefining
 *	default_stderr.  (XXXDCL Though now that I think of it, the default
 *	logging category can be changed with only one additional function
 *	call by defining a new channel and then calling isc_log_usechannel()
 *	for #ISC_LOGCATEGORY_DEFAULT.)
 *
 *\li	Specifying #ISC_LOG_PRINTTIME or #ISC_LOG_PRINTTAG for syslog is allowed,
 *	but probably not what you wanted to do.
 *
 *	#ISC_LOG_DEBUGONLY will mark the channel as usable only when the
 *	debug level of the logging context (see isc_log_setdebuglevel)
 *	is non-zero.
 *
 * Requires:
 *\li	lcfg is a valid logging configuration.
 *
 *\li	name is not NULL.
 *
 *\li	type is #ISC_LOG_TOSYSLOG, #ISC_LOG_TOFILE, #ISC_LOG_TOFILEDESC or
 *		#ISC_LOG_TONULL.
 *
 *\li	destination is not NULL unless type is #ISC_LOG_TONULL.
 *
 *\li	level is >= #ISC_LOG_CRITICAL (the most negative logging level).
 *
 *\li	flags does not include any bits aside from the ISC_LOG_PRINT* bits
 *	or #ISC_LOG_DEBUGONLY.
 *
 * Ensures:
 *\li	#ISC_R_SUCCESS
 *		A channel with the given name is usable with
 *		isc_log_usechannel().
 *
 *\li	#ISC_R_NOMEMORY or #ISC_R_UNEXPECTED
 *		No additional memory is being used by the logging context.
 *		Any channel that previously existed with the given name
 *		is not redefined.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		Success
 *\li	#ISC_R_NOMEMORY		Resource limit: Out of memory
 *\li	#ISC_R_UNEXPECTED	type was out of range and REQUIRE()
 *					was disabled.
 */

isc_result_t
isc_log_usechannel(isc_logconfig_t *lcfg, const char *name,
		   const isc_logcategory_t *category,
		   const isc_logmodule_t *module);
/*%<
 * Associate a named logging channel with a category and module that
 * will use it.
 *
 * Notes:
 *\li	The name is searched for linearly in the set of known channel names
 *	until a match is found.  (Note the performance impact of a very large
 *	number of named channels.)  When multiple channels of the same
 *	name are defined, the most recent definition is found.
 *
 *\li	Specifying a very large number of channels for a category will have
 *	a moderate impact on performance in isc_log_write(), as each
 *	call looks up the category for the start of a linked list, which
 *	it follows all the way to the end to find matching modules.  The
 *	test for matching modules is  integral, though.
 *
 *\li	If category is NULL, then the channel is associated with the indicated
 *	module for all known categories (including the "default" category).
 *
 *\li	If module is NULL, then the channel is associated with every module
 *	that uses that category.
 *
 *\li	Passing both category and module as NULL would make every log message
 *	use the indicated channel.
 *
 * \li	Specifying a channel that is #ISC_LOG_TONULL for a category/module pair
 *	has no effect on any other channels associated with that pair,
 *	regardless of ordering.  Thus you cannot use it to "mask out" one
 *	category/module pair when you have specified some other channel that
 * 	is also used by that category/module pair.
 *
 * Requires:
 *\li	lcfg is a valid logging configuration.
 *
 *\li	category is NULL or has an id that is in the range of known ids.
 *
 *	module is NULL or has an id that is in the range of known ids.
 *
 * Ensures:
 *\li	#ISC_R_SUCCESS
 *		The channel will be used by the indicated category/module
 *		arguments.
 *
 *\li	#ISC_R_NOMEMORY
 *		If assignment for a specific category has been requested,
 *		the channel has not been associated with the indicated
 *		category/module arguments and no additional memory is
 *		used by the logging context.
 *		If assignment for all categories has been requested
 *		then _some_ may have succeeded (starting with category
 *		"default" and progressing through the order of categories
 *		passed to isc_log_registercategories()) and additional memory
 *		is being used by whatever assignments succeeded.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	Success
 *\li	#ISC_R_NOMEMORY	Resource limit: Out of memory
 */

/* Attention: next four comments PRECEED code */
/*!
 *   \brief
 * Write a message to the log channels.
 *
 * Notes:
 *\li	Log messages containing natural language text should be logged with
 *	isc_log_iwrite() to allow for localization.
 *
 *\li	lctx can be NULL; this is allowed so that programs which use
 *	libraries that use the ISC logging system are not required to
 *	also use it.
 *
 *\li	The format argument is a printf(3) string, with additional arguments
 *	as necessary.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *
 *\li	The category and module arguments must have ids that are in the
 *	range of known ids, as established by isc_log_registercategories()
 *	and isc_log_registermodules().
 *
 *\li	level != #ISC_LOG_DYNAMIC.  ISC_LOG_DYNAMIC is used only to define
 *	channels, and explicit debugging level must be identified for
 *	isc_log_write() via ISC_LOG_DEBUG(level).
 *
 *\li	format != NULL.
 *
 * Ensures:
 *\li	The log message is written to every channel associated with the
 *	indicated category/module pair.
 *
 * Returns:
 *\li	Nothing.  Failure to log a message is not construed as a
 *	meaningful error.
 */
void
isc_log_write(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level,
	      const char *format, ...)

ISC_FORMAT_PRINTF(5, 6);

/*%
 * Write a message to the log channels.
 *
 * Notes:
 *\li	lctx can be NULL; this is allowed so that programs which use
 *	libraries that use the ISC logging system are not required to
 *	also use it.
 *
 *\li	The format argument is a printf(3) string, with additional arguments
 *	as necessary.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *
 *\li	The category and module arguments must have ids that are in the
 *	range of known ids, as established by isc_log_registercategories()
 *	and isc_log_registermodules().
 *
 *\li	level != #ISC_LOG_DYNAMIC.  ISC_LOG_DYNAMIC is used only to define
 *	channels, and explicit debugging level must be identified for
 *	isc_log_write() via ISC_LOG_DEBUG(level).
 *
 *\li	format != NULL.
 *
 * Ensures:
 *\li	The log message is written to every channel associated with the
 *	indicated category/module pair.
 *
 * Returns:
 *\li	Nothing.  Failure to log a message is not construed as a
 *	meaningful error.
 */
void
isc_log_vwrite(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level,
	       const char *format, va_list args)

ISC_FORMAT_PRINTF(5, 0);

/*%
 * Write a message to the log channels, pruning duplicates that occur within
 * a configurable amount of seconds (see isc_log_[sg]etduplicateinterval).
 * This function is otherwise identical to isc_log_write().
 */
void
isc_log_write1(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level, const char *format, ...)

ISC_FORMAT_PRINTF(5, 6);

/*%
 * Write a message to the log channels, pruning duplicates that occur within
 * a configurable amount of seconds (see isc_log_[sg]etduplicateinterval).
 * This function is otherwise identical to isc_log_vwrite().
 */
void
isc_log_vwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		isc_logmodule_t *module, int level, const char *format,
		va_list args)

ISC_FORMAT_PRINTF(5, 0);

/*%
 * These are four internationalized versions of the isc_log_[v]write[1]
 * functions.
 *
 * The only difference is that they take arguments for a message
 * catalog, message set, and message number, all immediately preceding the
 * format argument.  The format argument becomes the default text, a la
 * isc_msgcat_get.  If the message catalog is NULL, no lookup is attempted
 * for a message -- which makes the message set and message number irrelevant,
 * and the non-internationalized call should have probably been used instead.
 *
 * Yes, that means there are now *eight* interfaces to logging a message.
 * Sheesh.   Make the madness stop!
 */
/*@{*/
void
isc_log_iwrite(isc_log_t *lctx, isc_logcategory_t *category,
	      isc_logmodule_t *module, int level,
	      isc_msgcat_t *msgcat, int msgset, int message,
	      const char *format, ...)
ISC_FORMAT_PRINTF(8, 9);

void
isc_log_ivwrite(isc_log_t *lctx, isc_logcategory_t *category,
		isc_logmodule_t *module, int level,
		isc_msgcat_t *msgcat, int msgset, int message,
		const char *format, va_list args)
ISC_FORMAT_PRINTF(8, 0);

void
isc_log_iwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		isc_logmodule_t *module, int level,
		isc_msgcat_t *msgcat, int msgset, int message,
		const char *format, ...)
ISC_FORMAT_PRINTF(8, 9);

void
isc_log_ivwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		 isc_logmodule_t *module, int level,
		 isc_msgcat_t *msgcat, int msgset, int message,
		 const char *format, va_list args)
ISC_FORMAT_PRINTF(8, 0);
/*@}*/

void
isc_log_setdebuglevel(isc_log_t *lctx, unsigned int level);
/*%<
 * Set the debugging level used for logging.
 *
 * Notes:
 *\li	Setting the debugging level to 0 disables debugging log messages.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *
 * Ensures:
 *\li	The debugging level is set to the requested value.
 */

unsigned int
isc_log_getdebuglevel(isc_log_t *lctx);
/*%<
 * Get the current debugging level.
 *
 * Notes:
 *\li	This is provided so that a program can have a notion of
 *	"increment debugging level" or "decrement debugging level"
 *	without needing to keep track of what the current level is.
 *
 *\li	A return value of 0 indicates that debugging messages are disabled.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *
 * Ensures:
 *\li	The current logging debugging level is returned.
 */

isc_boolean_t
isc_log_wouldlog(isc_log_t *lctx, int level);
/*%<
 * Determine whether logging something to 'lctx' at 'level' would
 * actually cause something to be logged somewhere.
 *
 * If #ISC_FALSE is returned, it is guaranteed that nothing would
 * be logged, allowing the caller to omit unnecessary
 * isc_log_write() calls and possible message preformatting.
 */

void
isc_log_setduplicateinterval(isc_logconfig_t *lcfg, unsigned int interval);
/*%<
 * Set the interval over which duplicate log messages will be ignored
 * by isc_log_[v]write1(), in seconds.
 *
 * Notes:
 *\li	Increasing the duplicate interval from X to Y will not necessarily
 *	filter out duplicates of messages logged in Y - X seconds since the
 *	increase.  (Example: Message1 is logged at midnight.  Message2
 *	is logged at 00:01:00, when the interval is only 30 seconds, causing
 *	Message1 to be expired from the log message history.  Then the interval
 *	is increased to 3000 (five minutes) and at 00:04:00 Message1 is logged
 *	again.  It will appear the second time even though less than five
 *	passed since the first occurrence.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 */

unsigned int
isc_log_getduplicateinterval(isc_logconfig_t *lcfg);
/*%<
 * Get the current duplicate filtering interval.
 *
 * Requires:
 *\li	lctx is a valid logging context.
 *
 * Returns:
 *\li	The current duplicate filtering interval.
 */

isc_result_t
isc_log_settag(isc_logconfig_t *lcfg, const char *tag);
/*%<
 * Set the program name or other identifier for #ISC_LOG_PRINTTAG.
 *
 * Requires:
 *\li	lcfg is a valid logging configuration.
 *
 * Notes:
 *\li	If this function has not set the tag to a non-NULL, non-empty value,
 *	then the #ISC_LOG_PRINTTAG channel flag will not print anything.
 *	Unlike some implementations of syslog on Unix systems, you *must* set
 *	the tag in order to get it logged.  It is not implicitly derived from
 *	the program name (which is pretty impossible to infer portably).
 *
 *\li	Setting the tag to NULL or the empty string will also cause the
 *	#ISC_LOG_PRINTTAG channel flag to not print anything.  If tag equals the
 *	empty string, calls to isc_log_gettag will return NULL.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	Success
 *\li	#ISC_R_NOMEMORY  Resource Limit: Out of memory
 *
 * XXXDCL when creating a new isc_logconfig_t, it might be nice if the tag
 * of the currently active isc_logconfig_t was inherited.  this does not
 * currently happen.
 */

char *
isc_log_gettag(isc_logconfig_t *lcfg);
/*%<
 * Get the current identifier printed with #ISC_LOG_PRINTTAG.
 *
 * Requires:
 *\li	lcfg is a valid logging configuration.
 *
 * Notes:
 *\li	Since isc_log_settag() will not associate a zero-length string
 *	with the logging configuration, attempts to do so will cause
 *	this function to return NULL.  However, a determined programmer
 *	will observe that (currently) a tag of length greater than zero
 *	could be set, and then modified to be zero length.
 *
 * Returns:
 *\li	A pointer to the current identifier, or NULL if none has been set.
 */

void
isc_log_opensyslog(const char *tag, int options, int facility);
/*%<
 * Initialize syslog logging.
 *
 * Notes:
 *\li	XXXDCL NT
 *	This is currently equivalent to openlog(), but is not going to remain
 *	that way.  In the meantime, the arguments are all identical to
 *	those used by openlog(3), as follows:
 *
 * \code
 *		tag: The string to use in the position of the program
 *			name in syslog messages.  Most (all?) syslogs
 *			will use basename(argv[0]) if tag is NULL.
 *
 *		options: LOG_CONS, LOG_PID, LOG_NDELAY ... whatever your
 *			syslog supports.
 *
 *		facility: The default syslog facility.  This is irrelevant
 *			since isc_log_write will ALWAYS use the channel's
 *			declared facility.
 * \endcode
 *
 *\li	Zero effort has been made (yet) to accommodate systems with openlog()
 *	that only takes two arguments, or to identify valid syslog
 *	facilities or options for any given architecture.
 *
 *\li	It is necessary to call isc_log_opensyslog() to initialize
 *	syslogging on machines which do not support network connections to
 *	syslogd because they require a Unix domain socket to be used.  Since
 *	this is a chore to determine at run-time, it is suggested that it
 *	always be called by programs using the ISC logging system.
 *
 * Requires:
 *\li	Nothing.
 *
 * Ensures:
 *\li	openlog() is called to initialize the syslog system.
 */

void
isc_log_closefilelogs(isc_log_t *lctx);
/*%<
 * Close all open files used by #ISC_LOG_TOFILE channels.
 *
 * Notes:
 *\li	This function is provided for programs that want to use their own
 *	log rolling mechanism rather than the one provided internally.
 *	For example, a program that wanted to keep daily logs would define
 *	a channel which used #ISC_LOG_ROLLNEVER, then once a day would
 *	rename the log file and call isc_log_closefilelogs().
 *
 *\li	#ISC_LOG_TOFILEDESC channels are unaffected.
 *
 * Requires:
 *\li	lctx is a valid context.
 *
 * Ensures:
 *\li	The open files are closed and will be reopened when they are
 *	next needed.
 */

isc_logcategory_t *
isc_log_categorybyname(isc_log_t *lctx, const char *name);
/*%<
 * Find a category by its name.
 *
 * Notes:
 *\li	The string name of a category is not required to be unique.
 *
 * Requires:
 *\li	lctx is a valid context.
 *\li	name is not NULL.
 *
 * Returns:
 *\li	A pointer to the _first_ isc_logcategory_t structure used by "name".
 *
 *\li	NULL if no category exists by that name.
 */

isc_logmodule_t *
isc_log_modulebyname(isc_log_t *lctx, const char *name);
/*%<
 * Find a module by its name.
 *
 * Notes:
 *\li	The string name of a module is not required to be unique.
 *
 * Requires:
 *\li	lctx is a valid context.
 *\li	name is not NULL.
 *
 * Returns:
 *\li	A pointer to the _first_ isc_logmodule_t structure used by "name".
 *
 *\li	NULL if no module exists by that name.
 */

void
isc_log_setcontext(isc_log_t *lctx);
/*%<
 * Sets the context used by the libisc for logging.
 *
 * Requires:
 *\li	lctx be a valid context.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_LOG_H */
