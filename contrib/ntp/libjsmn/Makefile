# You can put your build options here
-include config.mk

all: libjsmn.a 

libjsmn.a: jsmn.o
	$(AR) rc $@ $^

%.o: %.c jsmn.h
	$(CC) -c $(CFLAGS) $< -o $@

test: jsmn_test
	./jsmn_test

jsmn_test: jsmn_test.o
	$(CC) $(LDFLAGS) -L. -ljsmn $< -o $@

jsmn_test.o: jsmn_test.c libjsmn.a

simple_example: example/simple.o libjsmn.a
	$(CC) $(LDFLAGS) $^ -o $@

jsondump: example/jsondump.o libjsmn.a
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f jsmn.o jsmn_test.o example/simple.o
	rm -f jsmn_test
	rm -f jsmn_test.exe
	rm -f libjsmn.a
	rm -f simple_example
	rm -f jsondump

.PHONY: all clean test

