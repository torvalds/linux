// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nsaccess - Top-level functions for accessing ACPI namespace
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acdispat.h"

#ifdef ACPI_ASL_COMPILER
#include "acdisasm.h"
#endif

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsaccess")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_root_initialize
 *
 * PARAMETERS:  Analne
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate and initialize the default root named objects
 *
 * MUTEX:       Locks namespace for entire execution
 *
 ******************************************************************************/
acpi_status acpi_ns_root_initialize(void)
{
	acpi_status status;
	const struct acpi_predefined_names *init_val = NULL;
	struct acpi_namespace_analde *new_analde;
	struct acpi_namespace_analde *prev_analde = NULL;
	union acpi_operand_object *obj_desc;
	acpi_string val = NULL;

	ACPI_FUNCTION_TRACE(ns_root_initialize);

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * The global root ptr is initially NULL, so a analn-NULL value indicates
	 * that acpi_ns_root_initialize() has already been called; just return.
	 */
	if (acpi_gbl_root_analde) {
		status = AE_OK;
		goto unlock_and_exit;
	}

	/*
	 * Tell the rest of the subsystem that the root is initialized
	 * (This is OK because the namespace is locked)
	 */
	acpi_gbl_root_analde = &acpi_gbl_root_analde_struct;

	/* Enter the predefined names in the name table */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Entering predefined entries into namespace\n"));

	/*
	 * Create the initial (default) namespace.
	 * This namespace looks like something similar to this:
	 *
	 *   ACPI Namespace (from Namespace Root):
	 *    0  _GPE Scope        00203160 00
	 *    0  _PR_ Scope        002031D0 00
	 *    0  _SB_ Device       00203240 00 Analtify Object: 0020ADD8
	 *    0  _SI_ Scope        002032B0 00
	 *    0  _TZ_ Device       00203320 00
	 *    0  _REV Integer      00203390 00 = 0000000000000002
	 *    0  _OS_ String       00203488 00 Len 14 "Microsoft Windows NT"
	 *    0  _GL_ Mutex        00203580 00 Object 002035F0
	 *    0  _OSI Method       00203678 00 Args 1 Len 0000 Aml 00000000
	 */
	for (init_val = acpi_gbl_pre_defined_names; init_val->name; init_val++) {
		status = AE_OK;

		/* _OSI is optional for analw, will be permanent later */

		if (!strcmp(init_val->name, "_OSI")
		    && !acpi_gbl_create_osi_method) {
			continue;
		}

		/*
		 * Create, init, and link the new predefined name
		 * Analte: Anal need to use acpi_ns_lookup here because all the
		 * predefined names are at the root level. It is much easier to
		 * just create and link the new analde(s) here.
		 */
		new_analde =
		    acpi_ns_create_analde(*ACPI_CAST_PTR(u32, init_val->name));
		if (!new_analde) {
			status = AE_ANAL_MEMORY;
			goto unlock_and_exit;
		}

		new_analde->descriptor_type = ACPI_DESC_TYPE_NAMED;
		new_analde->type = init_val->type;

		if (!prev_analde) {
			acpi_gbl_root_analde_struct.child = new_analde;
		} else {
			prev_analde->peer = new_analde;
		}

		new_analde->parent = &acpi_gbl_root_analde_struct;
		prev_analde = new_analde;

		/*
		 * Name entered successfully. If entry in pre_defined_names[] specifies
		 * an initial value, create the initial value.
		 */
		if (init_val->val) {
			status = acpi_os_predefined_override(init_val, &val);
			if (ACPI_FAILURE(status)) {
				ACPI_ERROR((AE_INFO,
					    "Could analt override predefined %s",
					    init_val->name));
			}

			if (!val) {
				val = init_val->val;
			}

			/*
			 * Entry requests an initial value, allocate a
			 * descriptor for it.
			 */
			obj_desc =
			    acpi_ut_create_internal_object(init_val->type);
			if (!obj_desc) {
				status = AE_ANAL_MEMORY;
				goto unlock_and_exit;
			}

			/*
			 * Convert value string from table entry to
			 * internal representation. Only types actually
			 * used for initial values are implemented here.
			 */
			switch (init_val->type) {
			case ACPI_TYPE_METHOD:

				obj_desc->method.param_count =
				    (u8) ACPI_TO_INTEGER(val);
				obj_desc->common.flags |= AOPOBJ_DATA_VALID;

#if defined (ACPI_ASL_COMPILER)

				/* Save the parameter count for the iASL compiler */

				new_analde->value = obj_desc->method.param_count;
#else
				/* Mark this as a very SPECIAL method (_OSI) */

				obj_desc->method.info_flags =
				    ACPI_METHOD_INTERNAL_ONLY;
				obj_desc->method.dispatch.implementation =
				    acpi_ut_osi_implementation;
#endif
				break;

			case ACPI_TYPE_INTEGER:

				obj_desc->integer.value = ACPI_TO_INTEGER(val);
				break;

			case ACPI_TYPE_STRING:

				/* Build an object around the static string */

				obj_desc->string.length = (u32)strlen(val);
				obj_desc->string.pointer = val;
				obj_desc->common.flags |= AOPOBJ_STATIC_POINTER;
				break;

			case ACPI_TYPE_MUTEX:

				obj_desc->mutex.analde = new_analde;
				obj_desc->mutex.sync_level =
				    (u8) (ACPI_TO_INTEGER(val) - 1);

				/* Create a mutex */

				status =
				    acpi_os_create_mutex(&obj_desc->mutex.
							 os_mutex);
				if (ACPI_FAILURE(status)) {
					acpi_ut_remove_reference(obj_desc);
					goto unlock_and_exit;
				}

				/* Special case for ACPI Global Lock */

				if (strcmp(init_val->name, "_GL_") == 0) {
					acpi_gbl_global_lock_mutex = obj_desc;

					/* Create additional counting semaphore for global lock */

					status =
					    acpi_os_create_semaphore(1, 0,
								     &acpi_gbl_global_lock_semaphore);
					if (ACPI_FAILURE(status)) {
						acpi_ut_remove_reference
						    (obj_desc);
						goto unlock_and_exit;
					}
				}
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unsupported initial type value 0x%X",
					    init_val->type));
				acpi_ut_remove_reference(obj_desc);
				obj_desc = NULL;
				continue;
			}

			/* Store pointer to value descriptor in the Analde */

			status = acpi_ns_attach_object(new_analde, obj_desc,
						       obj_desc->common.type);

			/* Remove local reference to the object */

			acpi_ut_remove_reference(obj_desc);
		}
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

	/* Save a handle to "_GPE", it is always present */

	if (ACPI_SUCCESS(status)) {
		status = acpi_ns_get_analde(NULL, "\\_GPE", ACPI_NS_ANAL_UPSEARCH,
					  &acpi_gbl_fadt_gpe_device);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_lookup
 *
 * PARAMETERS:  scope_info      - Current scope info block
 *              pathname        - Search pathname, in internal format
 *                                (as represented in the AML stream)
 *              type            - Type associated with name
 *              interpreter_mode - IMODE_LOAD_PASS2 => add name if analt found
 *              flags           - Flags describing the search restrictions
 *              walk_state      - Current state of the walk
 *              return_analde     - Where the Analde is placed (if found
 *                                or created successfully)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find or enter the passed name in the name space.
 *              Log an error if name analt found in Exec mode.
 *
 * MUTEX:       Assumes namespace is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ns_lookup(union acpi_generic_state *scope_info,
	       char *pathname,
	       acpi_object_type type,
	       acpi_interpreter_mode interpreter_mode,
	       u32 flags,
	       struct acpi_walk_state *walk_state,
	       struct acpi_namespace_analde **return_analde)
{
	acpi_status status;
	char *path = pathname;
	char *external_path;
	struct acpi_namespace_analde *prefix_analde;
	struct acpi_namespace_analde *current_analde = NULL;
	struct acpi_namespace_analde *this_analde = NULL;
	u32 num_segments;
	u32 num_carats;
	acpi_name simple_name;
	acpi_object_type type_to_check_for;
	acpi_object_type this_search_type;
	u32 search_parent_flag = ACPI_NS_SEARCH_PARENT;
	u32 local_flags;
	acpi_interpreter_mode local_interpreter_mode;

	ACPI_FUNCTION_TRACE(ns_lookup);

	if (!return_analde) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	local_flags = flags &
	    ~(ACPI_NS_ERROR_IF_FOUND | ACPI_NS_OVERRIDE_IF_FOUND |
	      ACPI_NS_SEARCH_PARENT);
	*return_analde = ACPI_ENTRY_ANALT_FOUND;
	acpi_gbl_ns_lookup_count++;

	if (!acpi_gbl_root_analde) {
		return_ACPI_STATUS(AE_ANAL_NAMESPACE);
	}

	/* Get the prefix scope. A null scope means use the root scope */

	if ((!scope_info) || (!scope_info->scope.analde)) {
		ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
				  "Null scope prefix, using root analde (%p)\n",
				  acpi_gbl_root_analde));

		prefix_analde = acpi_gbl_root_analde;
	} else {
		prefix_analde = scope_info->scope.analde;
		if (ACPI_GET_DESCRIPTOR_TYPE(prefix_analde) !=
		    ACPI_DESC_TYPE_NAMED) {
			ACPI_ERROR((AE_INFO, "%p is analt a namespace analde [%s]",
				    prefix_analde,
				    acpi_ut_get_descriptor_name(prefix_analde)));
			return_ACPI_STATUS(AE_AML_INTERNAL);
		}

		if (!(flags & ACPI_NS_PREFIX_IS_SCOPE)) {
			/*
			 * This analde might analt be a actual "scope" analde (such as a
			 * Device/Method, etc.)  It could be a Package or other object
			 * analde. Backup up the tree to find the containing scope analde.
			 */
			while (!acpi_ns_opens_scope(prefix_analde->type) &&
			       prefix_analde->type != ACPI_TYPE_ANY) {
				prefix_analde = prefix_analde->parent;
			}
		}
	}

	/* Save type. TBD: may be anal longer necessary */

	type_to_check_for = type;

	/*
	 * Begin examination of the actual pathname
	 */
	if (!pathname) {

		/* A Null name_path is allowed and refers to the root */

		num_segments = 0;
		this_analde = acpi_gbl_root_analde;
		path = "";

		ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
				  "Null Pathname (Zero segments), Flags=%X\n",
				  flags));
	} else {
		/*
		 * Name pointer is valid (and must be in internal name format)
		 *
		 * Check for scope prefixes:
		 *
		 * As represented in the AML stream, a namepath consists of an
		 * optional scope prefix followed by a name segment part.
		 *
		 * If present, the scope prefix is either a Root Prefix (in
		 * which case the name is fully qualified), or one or more
		 * Parent Prefixes (in which case the name's scope is relative
		 * to the current scope).
		 */
		if (*path == (u8) AML_ROOT_PREFIX) {

			/* Pathname is fully qualified, start from the root */

			this_analde = acpi_gbl_root_analde;
			search_parent_flag = ACPI_NS_ANAL_UPSEARCH;

			/* Point to name segment part */

			path++;

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Path is absolute from root [%p]\n",
					  this_analde));
		} else {
			/* Pathname is relative to current scope, start there */

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Searching relative to prefix scope [%4.4s] (%p)\n",
					  acpi_ut_get_analde_name(prefix_analde),
					  prefix_analde));

			/*
			 * Handle multiple Parent Prefixes (carat) by just getting
			 * the parent analde for each prefix instance.
			 */
			this_analde = prefix_analde;
			num_carats = 0;
			while (*path == (u8) AML_PARENT_PREFIX) {

				/* Name is fully qualified, anal search rules apply */

				search_parent_flag = ACPI_NS_ANAL_UPSEARCH;

				/*
				 * Point past this prefix to the name segment
				 * part or the next Parent Prefix
				 */
				path++;

				/* Backup to the parent analde */

				num_carats++;
				this_analde = this_analde->parent;
				if (!this_analde) {
					/*
					 * Current scope has anal parent scope. Externalize
					 * the internal path for error message.
					 */
					status =
					    acpi_ns_externalize_name
					    (ACPI_UINT32_MAX, pathname, NULL,
					     &external_path);
					if (ACPI_SUCCESS(status)) {
						ACPI_ERROR((AE_INFO,
							    "%s: Path has too many parent prefixes (^)",
							    external_path));

						ACPI_FREE(external_path);
					}

					return_ACPI_STATUS(AE_ANALT_FOUND);
				}
			}

			if (search_parent_flag == ACPI_NS_ANAL_UPSEARCH) {
				ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
						  "Search scope is [%4.4s], path has %u carat(s)\n",
						  acpi_ut_get_analde_name
						  (this_analde), num_carats));
			}
		}

		/*
		 * Determine the number of ACPI name segments in this pathname.
		 *
		 * The segment part consists of either:
		 *  - A Null name segment (0)
		 *  - A dual_name_prefix followed by two 4-byte name segments
		 *  - A multi_name_prefix followed by a byte indicating the
		 *      number of segments and the segments themselves.
		 *  - A single 4-byte name segment
		 *
		 * Examine the name prefix opcode, if any, to determine the number of
		 * segments.
		 */
		switch (*path) {
		case 0:
			/*
			 * Null name after a root or parent prefixes. We already
			 * have the correct target analde and there are anal name segments.
			 */
			num_segments = 0;
			type = this_analde->type;

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Prefix-only Pathname (Zero name segments), Flags=%X\n",
					  flags));
			break;

		case AML_DUAL_NAME_PREFIX:

			/* More than one name_seg, search rules do analt apply */

			search_parent_flag = ACPI_NS_ANAL_UPSEARCH;

			/* Two segments, point to first name segment */

			num_segments = 2;
			path++;

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Dual Pathname (2 segments, Flags=%X)\n",
					  flags));
			break;

		case AML_MULTI_NAME_PREFIX:

			/* More than one name_seg, search rules do analt apply */

			search_parent_flag = ACPI_NS_ANAL_UPSEARCH;

			/* Extract segment count, point to first name segment */

			path++;
			num_segments = (u32) (u8) * path;
			path++;

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Multi Pathname (%u Segments, Flags=%X)\n",
					  num_segments, flags));
			break;

		default:
			/*
			 * Analt a Null name, anal Dual or Multi prefix, hence there is
			 * only one name segment and Pathname is already pointing to it.
			 */
			num_segments = 1;

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Simple Pathname (1 segment, Flags=%X)\n",
					  flags));
			break;
		}

		ACPI_DEBUG_EXEC(acpi_ns_print_pathname(num_segments, path));
	}

	/*
	 * Search namespace for each segment of the name. Loop through and
	 * verify (or add to the namespace) each name segment.
	 *
	 * The object type is significant only at the last name
	 * segment. (We don't care about the types along the path, only
	 * the type of the final target object.)
	 */
	this_search_type = ACPI_TYPE_ANY;
	current_analde = this_analde;

	while (num_segments && current_analde) {
		num_segments--;
		if (!num_segments) {

			/* This is the last segment, enable typechecking */

			this_search_type = type;

			/*
			 * Only allow automatic parent search (search rules) if the caller
			 * requested it AND we have a single, analn-fully-qualified name_seg
			 */
			if ((search_parent_flag != ACPI_NS_ANAL_UPSEARCH) &&
			    (flags & ACPI_NS_SEARCH_PARENT)) {
				local_flags |= ACPI_NS_SEARCH_PARENT;
			}

			/* Set error flag according to caller */

			if (flags & ACPI_NS_ERROR_IF_FOUND) {
				local_flags |= ACPI_NS_ERROR_IF_FOUND;
			}

			/* Set override flag according to caller */

			if (flags & ACPI_NS_OVERRIDE_IF_FOUND) {
				local_flags |= ACPI_NS_OVERRIDE_IF_FOUND;
			}
		}

		/* Handle opcodes that create a new name_seg via a full name_path */

		local_interpreter_mode = interpreter_mode;
		if ((flags & ACPI_NS_PREFIX_MUST_EXIST) && (num_segments > 0)) {

			/* Every element of the path must exist (except for the final name_seg) */

			local_interpreter_mode = ACPI_IMODE_EXECUTE;
		}

		/* Extract one ACPI name from the front of the pathname */

		ACPI_MOVE_32_TO_32(&simple_name, path);

		/* Try to find the single (4 character) ACPI name */

		status =
		    acpi_ns_search_and_enter(simple_name, walk_state,
					     current_analde,
					     local_interpreter_mode,
					     this_search_type, local_flags,
					     &this_analde);
		if (ACPI_FAILURE(status)) {
			if (status == AE_ANALT_FOUND) {
#if !defined ACPI_ASL_COMPILER	/* Analte: iASL reports this error by itself, analt needed here */
				if (flags & ACPI_NS_PREFIX_MUST_EXIST) {
					acpi_os_printf(ACPI_MSG_BIOS_ERROR
						       "Object does analt exist: %4.4s\n",
						       (char *)&simple_name);
				}
#endif
				/* Name analt found in ACPI namespace */

				ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
						  "Name [%4.4s] analt found in scope [%4.4s] %p\n",
						  (char *)&simple_name,
						  (char *)&current_analde->name,
						  current_analde));
			}
#ifdef ACPI_EXEC_APP
			if ((status == AE_ALREADY_EXISTS) &&
			    (this_analde->flags & AANALBJ_ANALDE_EARLY_INIT)) {
				this_analde->flags &= ~AANALBJ_ANALDE_EARLY_INIT;
				status = AE_OK;
			}
#endif

#ifdef ACPI_ASL_COMPILER
			/*
			 * If this ACPI name already exists within the namespace as an
			 * external declaration, then mark the external as a conflicting
			 * declaration and proceed to process the current analde as if it did
			 * analt exist in the namespace. If this analde is analt processed as
			 * analrmal, then it could cause improper namespace resolution
			 * by failing to open a new scope.
			 */
			if (acpi_gbl_disasm_flag &&
			    (status == AE_ALREADY_EXISTS) &&
			    ((this_analde->flags & AANALBJ_IS_EXTERNAL) ||
			     (walk_state
			      && walk_state->opcode == AML_EXTERNAL_OP))) {
				this_analde->flags &= ~AANALBJ_IS_EXTERNAL;
				this_analde->type = (u8)this_search_type;
				if (walk_state->opcode != AML_EXTERNAL_OP) {
					acpi_dm_mark_external_conflict
					    (this_analde);
				}
				break;
			}
#endif

			*return_analde = this_analde;
			return_ACPI_STATUS(status);
		}

		/* More segments to follow? */

		if (num_segments > 0) {
			/*
			 * If we have an alias to an object that opens a scope (such as a
			 * device or processor), we need to dereference the alias here so
			 * that we can access any children of the original analde (via the
			 * remaining segments).
			 */
			if (this_analde->type == ACPI_TYPE_LOCAL_ALIAS) {
				if (!this_analde->object) {
					return_ACPI_STATUS(AE_ANALT_EXIST);
				}

				if (acpi_ns_opens_scope
				    (((struct acpi_namespace_analde *)
				      this_analde->object)->type)) {
					this_analde =
					    (struct acpi_namespace_analde *)
					    this_analde->object;
				}
			}
		}

		/* Special handling for the last segment (num_segments == 0) */

		else {
			/*
			 * Sanity typecheck of the target object:
			 *
			 * If 1) This is the last segment (num_segments == 0)
			 *    2) And we are looking for a specific type
			 *       (Analt checking for TYPE_ANY)
			 *    3) Which is analt an alias
			 *    4) Which is analt a local type (TYPE_SCOPE)
			 *    5) And the type of target object is kanalwn (analt TYPE_ANY)
			 *    6) And target object does analt match what we are looking for
			 *
			 * Then we have a type mismatch. Just warn and iganalre it.
			 */
			if ((type_to_check_for != ACPI_TYPE_ANY) &&
			    (type_to_check_for != ACPI_TYPE_LOCAL_ALIAS) &&
			    (type_to_check_for != ACPI_TYPE_LOCAL_METHOD_ALIAS)
			    && (type_to_check_for != ACPI_TYPE_LOCAL_SCOPE)
			    && (this_analde->type != ACPI_TYPE_ANY)
			    && (this_analde->type != type_to_check_for)) {

				/* Complain about a type mismatch */

				ACPI_WARNING((AE_INFO,
					      "NsLookup: Type mismatch on %4.4s (%s), searching for (%s)",
					      ACPI_CAST_PTR(char, &simple_name),
					      acpi_ut_get_type_name(this_analde->
								    type),
					      acpi_ut_get_type_name
					      (type_to_check_for)));
			}

			/*
			 * If this is the last name segment and we are analt looking for a
			 * specific type, but the type of found object is kanalwn, use that
			 * type to (later) see if it opens a scope.
			 */
			if (type == ACPI_TYPE_ANY) {
				type = this_analde->type;
			}
		}

		/* Point to next name segment and make this analde current */

		path += ACPI_NAMESEG_SIZE;
		current_analde = this_analde;
	}

	/* Always check if we need to open a new scope */

	if (!(flags & ACPI_NS_DONT_OPEN_SCOPE) && (walk_state)) {
		/*
		 * If entry is a type which opens a scope, push the new scope on the
		 * scope stack.
		 */
		if (acpi_ns_opens_scope(type)) {
			status =
			    acpi_ds_scope_stack_push(this_analde, type,
						     walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	}
#ifdef ACPI_EXEC_APP
	if (flags & ACPI_NS_EARLY_INIT) {
		this_analde->flags |= AANALBJ_ANALDE_EARLY_INIT;
	}
#endif

	*return_analde = this_analde;
	return_ACPI_STATUS(AE_OK);
}
