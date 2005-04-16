#!/bin/sh

cat <<EOF
/* Generated file for OUI database */

#include <linux/config.h>

#ifdef CONFIG_IEEE1394_OUI_DB
struct oui_list_struct {
	int oui;
	char *name;
} oui_list[] = {
EOF

while read oui name; do
	echo "	{ 0x$oui, \"$name\" },"
done

cat <<EOF
};

#endif /* CONFIG_IEEE1394_OUI_DB */
EOF
