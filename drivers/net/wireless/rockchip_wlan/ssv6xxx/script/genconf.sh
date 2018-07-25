#!/bin/bash
# Script to convert defines in compiler option in to C's defines
# Should be executed in make file and it take ccflags-y as the
# compiler options. The content will be redirected to the first arguement.

echo "#ifndef __SSV_MOD_CONF_H__" > $1
echo "#define __SSV_MOD_CONF_H__" >> $1

for flag in ${ccflags-y}; do
	if [[ "$flag" =~ ^-D.* ]]; then
		#def=${flag//-D/}
		def=${flag:2}
		echo "#ifndef $def" >> $1
		echo "#define $def" >> $1
		echo "#endif" >> $1
	fi
done
echo "#endif // __SSV_MOD_CONF_H__" >> $1
