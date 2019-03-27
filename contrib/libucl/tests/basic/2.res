section1 {
    param1 = "value";
    param2 = "value";
    section3 {
        param = "value";
        param2 = "value";
        param3 [
            "value1",
            "value2",
            100500,
        ]
    }
}
section2 {
    param1 {
        key = "value";
    }
    param1 [
        "key",
    ]
}
key1 = 1.0;
key1 = "some string";
key2 = 60.0;
key2 = "/some/path";
key3 = 1024;
key3 = "111some";
key4 = 5000000;
key4 = "s1";
key5 = 0.010000;
key5 = "\n\r123";
key6 = 315360000.0;
keyvar = "unknowntest";
keyvar = "unknownunknownunknown${unknown}";
keyvar = "${some}$no${}$$test$$$$$$$";
keyvar = "unknown$ABI$unknown$$";

