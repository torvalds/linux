#!/bin/bash
find . \( -not -path "*.output*" -a -name '*.[oas]' -o -name core -o -name '.*.flags' -o -name '.ko' -o -name '.*.cmd' -o -name 'Module.symvers' -o -name 'modules.order' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
find . \( -name '.tmp_versions' \) -type d -print | grep -v lxdialog/ | xargs rm -rf

