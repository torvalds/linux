#!/bin/sh

m4 divmod.m4 | sed -e 's/[[:space:]]*$//' | grep -v '^$' > modsi3.S
m4 -DANSWER=quotient divmod.m4 | sed -e 's/[[:space:]]*$//' | grep -v '^$' > divsi3.S
echo '! This file intentionally left blank' > umodsi3.S
echo '! This file intentionally left blank' > udivsi3.S
