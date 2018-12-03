# this make file is simply to help autogenerate these files:
# 	ni_route_values.h
#	ni_device_routes.h
# in order to do this, we are also generating a python representation (using
# ctypesgen) of ../../comedi.h.
# This allows us to sort NI signal/terminal names numerically to use a binary
# search through the device_routes tables to find valid routes.

ALL:
	@echo Typical targets:
	@echo "\`make csv-files\`"
	@echo "  Creates new csv-files using content of c-files of existing"
	@echo "  ni_routing/* content.  New csv files are placed in csv"
	@echo "  sub-directory."
	@echo "\`make c-files\`"
	@echo "  Creates new c-files using content of csv sub-directory.  These"
	@echo "  new c-files can be compared to the active content in the"
	@echo "  ni_routing directory."
	@echo "\`make csv-blank\`"
	@echo "  Create a new blank csv file.  This is useful for establishing a"
	@echo "  new data table for either a device family \(less likely\) or a"
	@echo "  specific board of an existing device family \(more likely\)."
	@echo "\`make clean-partial\`"
	@echo "  Remove all generated files/directories EXCEPT for csv/c files."
	@echo "\`make clean\`"
	@echo "  Remove all generated files/directories."
	@echo "\`make everything\`"
	@echo "  Build all csv-files, then all new c-files."

everything : csv-files c-files csv-blank

CPPFLAGS=-D"BIT(x)=(1UL<<(x))" -D__user=

comedi_h.py : ../../../comedi.h
	ctypesgen $< --include "sys/ioctl.h" --cpp 'gcc -E $(CPPFLAGS)' -o $@

convert_c_to_py: all_cfiles.c
	gcc -g convert_c_to_py.c -o convert_c_to_py -std=c99

ni_values.py: convert_c_to_py
	./convert_c_to_py

csv-files : ni_values.py comedi_h.py
	./convert_py_to_csv.py

csv-blank :
	./make_blank_csv.py
	@echo New blank csv signal table in csv/blank_route_table.csv

c-files : comedi_h.py
	./convert_csv_to_c.py --route_values --device_routes

ROUTE_VALUES_SRC=$(wildcard ../ni_route_values/*.c)
DEVICE_ROUTES_SRC=$(wildcard ../ni_device_routes/*.c)
all_cfiles.c : $(DEVICE_ROUTES_SRC) $(ROUTE_VALUES_SRC)
	@for i in $(DEVICE_ROUTES_SRC) $(ROUTE_VALUES_SRC); do \
		echo "#include \"$$i\"" >> all_cfiles.c; \
	done

clean-partial :
	$(RM) -rf comedi_h.py ni_values.py convert_c_to_py all_cfiles.c *.pyc \
		__pycache__/

clean : partial_clean
	$(RM) -rf c/ csv/

# Note:  One could also use ctypeslib in order to generate these files.  The
# caveat is that ctypeslib does not do a great job at handling macro functions.
# The make rules are as follows:
# comedi.h.xml : ../../comedi.h
# 	# note that we have to use PWD here to avoid h2xml finding a system
# 	# installed version of the comedilib/comedi.h file
# 	h2xml ${PWD}/../../comedi.h -c -D__user="" -D"BIT(x)=(1<<(x))" \
# 		-o comedi.h.xml
#
# comedi_h.py : comedi.h.xml
# 	xml2py ./comedi.h.xml -o comedi_h.py
# clean :
# 	rm -f comedi.h.xml comedi_h.py comedi_h.pyc
