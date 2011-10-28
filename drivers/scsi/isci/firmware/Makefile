# Makefile for create_fw
#
CC=gcc
CFLAGS=-c -Wall -O2 -g
LDFLAGS=
SOURCES=create_fw.c
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=create_fw

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -O $@

clean:
	rm -f *.o $(EXECUTABLE)
