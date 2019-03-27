# NB. libssh and libopenbsd-compat should be built with the same sanitizer opts.
CXX=clang++-3.9
FUZZ_FLAGS=-fsanitize=address,undefined -fsanitize-coverage=edge
FUZZ_LIBS=-lFuzzer

CXXFLAGS=-O2 -g -Wall -Wextra -I ../../.. $(FUZZ_FLAGS)
LDFLAGS=-L ../../.. -L ../../../openbsd-compat -g $(FUZZ_FLAGS)
LIBS=-lssh -lopenbsd-compat -lcrypto $(FUZZ_LIBS)

all: pubkey_fuzz sig_fuzz

.cc.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

pubkey_fuzz: pubkey_fuzz.o
	$(CXX) -o $@ pubkey_fuzz.o $(LDFLAGS) $(LIBS)

sig_fuzz: sig_fuzz.o
	$(CXX) -o $@ sig_fuzz.o $(LDFLAGS) $(LIBS)

clean:
	-rm -f *.o pubkey_fuzz sig_fuzz
