#!/bin/bash
# Script to convert defines in compiler option in to C's defines
# Should be executed in make file and it take ccflags-y as the
# compiler options. The content will be redirected to the first arguement.

temp=$1_temp

echo "#ifndef __SSV_CONF_PARSER_H__" > $temp
echo "#define __SSV_CONF_PARSER_H__" >> $temp

echo "char const *conf_parser[] = {" >> $temp

for flag in ${ccflags-y}; do
	if [[ "$flag" =~ ^-D.* ]]; then
		def=${flag:2}
        if [[ "$def" =~ .= ]]; then
            def_1=${def/\=/_}
            echo "\"$def_1\"," >> $temp
        else
		    echo "\"$def\"," >> $temp
        fi
	fi
done

echo "\"\"};" >> $temp

echo "#endif // __SSV_CONF_PARSER_H__" >> $temp
if [ -f $1 ];
then
	DIFF=$(diff $1 $temp)
	if [ "$DIFF" == "" ]; then
    		rm $temp
	else
    		mv $temp $1
	fi
else
	mv $temp $1
fi
