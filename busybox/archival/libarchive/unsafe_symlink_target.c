/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC create_or_remember_link(llist_t **link_placeholders,
		const char *target,
		const char *linkname,
		int hard_link)
{
	if (hard_link || target[0] == '/' || strstr(target, "..")) {
		llist_add_to_end(link_placeholders,
			xasprintf("%c%s%c%s", hard_link, linkname, '\0', target)
		);
		return;
	}
	if (symlink(target, linkname) != 0) {
		/* shared message */
		bb_perror_msg_and_die("can't create %slink '%s' to '%s'",
			"sym", linkname, target
		);
	}
}

void FAST_FUNC create_links_from_list(llist_t *list)
{
	while (list) {
		char *target;

		target = list->data + 1 + strlen(list->data + 1) + 1;
		if ((*list->data ? link : symlink) (target, list->data + 1)) {
			/* shared message */
			bb_error_msg_and_die("can't create %slink '%s' to '%s'",
				*list->data ? "hard" : "sym",
				list->data + 1, target
			);
		}
		list = list->link;
	}
}
