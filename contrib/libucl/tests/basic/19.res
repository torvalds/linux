okey_append {
    key = "value";
}
okey_append {
    key = "value1";
    key1 = "value2";
}
akey_append [
    "value",
]
akey_append [
    "value3",
]
skey_append = "value";
skey_append = "value4";
okey_merge {
    key = "value";
    key = "value1";
    source = "original";
    key1 = "value2";
}
akey_merge [
    "value",
    "value3",
]
skey_merge = "value";
skey_merge = "value4";
okey_rewrite {
    key = "value1";
    key1 = "value2";
}
akey_rewrite [
    "value3",
]
skey_rewrite = "value4";

