/* test .data.l[x] */
typedef struct {
        int type;
        union {
                char b[20];
                short s[10];
                long l[5];
	} data;
} foo;


foo bar = {
            .type = 3,
            .data.l[0] = 4
};
