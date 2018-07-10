#!/bin/sh

exec >NOFORK_NOEXEC.lst1

false && grep -Fv 'NOFORK' NOFORK_NOEXEC.lst \
| grep -v 'noexec.' | grep -v 'noexec$' \
| grep -v ' suid' \
| grep -v ' daemon' \
| grep -v ' longterm' \
| grep rare

echo === nofork candidate
grep -F 'nofork candidate' NOFORK_NOEXEC.lst \

echo === noexec candidate
grep -F 'noexec candidate' NOFORK_NOEXEC.lst \

echo === ^C
grep -F '^C' NOFORK_NOEXEC.lst \
| grep -F ' - ' \

echo === talks
grep -F 'talks' NOFORK_NOEXEC.lst \
| grep -F ' - ' \

echo ===
grep -Fv 'NOFORK' NOFORK_NOEXEC.lst \
| grep '^[^ ][^ ]* - ' \
| grep -v 'noexec.' | grep -v ' - noexec$' \
| grep -v ' suid' \
| grep -v ' daemon' \
| grep -v 'longterm' \
| grep -v 'interactive' \
| grep -v 'hardware' \
