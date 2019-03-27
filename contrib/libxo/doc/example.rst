
Examples
========

Unit Test
---------

Here is one of the unit tests as an example::

    int
    main (int argc, char **argv)
    {
        static char base_grocery[] = "GRO";
        static char base_hardware[] = "HRD";
        struct item {
            const char *i_title;
            int i_sold;
            int i_instock;
            int i_onorder;
            const char *i_sku_base;
            int i_sku_num;
        };
        struct item list[] = {
            { "gum", 1412, 54, 10, base_grocery, 415 },
            { "rope", 85, 4, 2, base_hardware, 212 },
            { "ladder", 0, 2, 1, base_hardware, 517 },
            { "bolt", 4123, 144, 42, base_hardware, 632 },
            { "water", 17, 14, 2, base_grocery, 2331 },
            { NULL, 0, 0, 0, NULL, 0 }
        };
        struct item list2[] = {
            { "fish", 1321, 45, 1, base_grocery, 533 },
        };
        struct item *ip;
        xo_info_t info[] = {
            { "in-stock", "number", "Number of items in stock" },
            { "name", "string", "Name of the item" },
            { "on-order", "number", "Number of items on order" },
            { "sku", "string", "Stock Keeping Unit" },
            { "sold", "number", "Number of items sold" },
            { NULL, NULL, NULL },
        };
        int info_count = (sizeof(info) / sizeof(info[0])) - 1;

        argc = xo_parse_args(argc, argv);
        if (argc < 0)
            exit(EXIT_FAILURE);

        xo_set_info(NULL, info, info_count);

        xo_open_container_h(NULL, "top");

        xo_open_container("data");
        xo_open_list("item");

        for (ip = list; ip->i_title; ip++) {
            xo_open_instance("item");

            xo_emit("{L:Item} '{k:name/%s}':\n", ip->i_title);
            xo_emit("{P:   }{L:Total sold}: {n:sold/%u%s}\n",
                    ip->i_sold, ip->i_sold ? ".0" : "");
            xo_emit("{P:   }{Lwc:In stock}{:in-stock/%u}\n",
                    ip->i_instock);
            xo_emit("{P:   }{Lwc:On order}{:on-order/%u}\n",
                    ip->i_onorder);
            xo_emit("{P:   }{L:SKU}: {q:sku/%s-000-%u}\n",
                    ip->i_sku_base, ip->i_sku_num);

            xo_close_instance("item");
        }

        xo_close_list("item");
        xo_close_container("data");

        xo_open_container("data");
        xo_open_list("item");

        for (ip = list2; ip->i_title; ip++) {
            xo_open_instance("item");

            xo_emit("{L:Item} '{:name/%s}':\n", ip->i_title);
            xo_emit("{P:   }{L:Total sold}: {n:sold/%u%s}\n",
                    ip->i_sold, ip->i_sold ? ".0" : "");
            xo_emit("{P:   }{Lwc:In stock}{:in-stock/%u}\n",
                    ip->i_instock);
            xo_emit("{P:   }{Lwc:On order}{:on-order/%u}\n",
                    ip->i_onorder);
            xo_emit("{P:   }{L:SKU}: {q:sku/%s-000-%u}\n",
                    ip->i_sku_base, ip->i_sku_num);

            xo_close_instance("item");
        }

        xo_close_list("item");
        xo_close_container("data");

        xo_close_container_h(NULL, "top");

        return 0;
    }

Text output::

    % ./testxo --libxo text
    Item 'gum':
       Total sold: 1412.0
       In stock: 54
       On order: 10
       SKU: GRO-000-415
    Item 'rope':
       Total sold: 85.0
       In stock: 4
       On order: 2
       SKU: HRD-000-212
    Item 'ladder':
       Total sold: 0
       In stock: 2
       On order: 1
       SKU: HRD-000-517
    Item 'bolt':
       Total sold: 4123.0
       In stock: 144
       On order: 42
       SKU: HRD-000-632
    Item 'water':
       Total sold: 17.0
       In stock: 14
       On order: 2
       SKU: GRO-000-2331
    Item 'fish':
       Total sold: 1321.0
       In stock: 45
       On order: 1
       SKU: GRO-000-533

JSON output::

    % ./testxo --libxo json,pretty
    "top": {
      "data": {
        "item": [
          {
            "name": "gum",
            "sold": 1412.0,
            "in-stock": 54,
            "on-order": 10,
            "sku": "GRO-000-415"
          },
          {
            "name": "rope",
            "sold": 85.0,
            "in-stock": 4,
            "on-order": 2,
            "sku": "HRD-000-212"
          },
          {
            "name": "ladder",
            "sold": 0,
            "in-stock": 2,
            "on-order": 1,
            "sku": "HRD-000-517"
          },
          {
            "name": "bolt",
            "sold": 4123.0,
            "in-stock": 144,
            "on-order": 42,
            "sku": "HRD-000-632"
          },
          {
            "name": "water",
            "sold": 17.0,
            "in-stock": 14,
            "on-order": 2,
            "sku": "GRO-000-2331"
          }
        ]
      },
      "data": {
        "item": [
          {
            "name": "fish",
            "sold": 1321.0,
            "in-stock": 45,
            "on-order": 1,
            "sku": "GRO-000-533"
          }
        ]
      }
    }

XML output::

    % ./testxo --libxo pretty,xml
    <top>
      <data>
        <item>
          <name>gum</name>
          <sold>1412.0</sold>
          <in-stock>54</in-stock>
          <on-order>10</on-order>
          <sku>GRO-000-415</sku>
        </item>
        <item>
          <name>rope</name>
          <sold>85.0</sold>
          <in-stock>4</in-stock>
          <on-order>2</on-order>
          <sku>HRD-000-212</sku>
        </item>
        <item>
          <name>ladder</name>
          <sold>0</sold>
          <in-stock>2</in-stock>
          <on-order>1</on-order>
          <sku>HRD-000-517</sku>
        </item>
        <item>
          <name>bolt</name>
          <sold>4123.0</sold>
          <in-stock>144</in-stock>
          <on-order>42</on-order>
          <sku>HRD-000-632</sku>
        </item>
        <item>
          <name>water</name>
          <sold>17.0</sold>
          <in-stock>14</in-stock>
          <on-order>2</on-order>
          <sku>GRO-000-2331</sku>
        </item>
      </data>
      <data>
        <item>
          <name>fish</name>
          <sold>1321.0</sold>
          <in-stock>45</in-stock>
          <on-order>1</on-order>
          <sku>GRO-000-533</sku>
        </item>
      </data>
    </top>

HMTL output::

    % ./testxo --libxo pretty,html
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">gum</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">1412.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">54</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">10</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">GRO-000-415</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">rope</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">85.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">4</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">HRD-000-212</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">ladder</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">1</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">HRD-000-517</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">bolt</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">4123.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">144</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">42</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">HRD-000-632</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">water</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">17.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">14</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">GRO-000-2331</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name">fish</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold">1321.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock">45</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order">1</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku">GRO-000-533</div>
    </div>

HTML output with xpath and info flags::

    % ./testxo --libxo pretty,html,xpath,info
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">gum</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">1412.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">54</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">10</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">GRO-000-415</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">rope</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">85.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">4</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">HRD-000-212</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">ladder</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">1</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">HRD-000-517</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">bolt</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">4123.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">144</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">42</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">HRD-000-632</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">water</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">17.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">14</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">2</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">GRO-000-2331</div>
    </div>
    <div class="line">
      <div class="label">Item</div>
      <div class="text"> '</div>
      <div class="data" data-tag="name"
           data-xpath="/top/data/item/name" data-type="string"
           data-help="Name of the item">fish</div>
      <div class="text">':</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">Total sold</div>
      <div class="text">: </div>
      <div class="data" data-tag="sold"
           data-xpath="/top/data/item/sold" data-type="number"
           data-help="Number of items sold">1321.0</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">In stock</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="in-stock"
           data-xpath="/top/data/item/in-stock" data-type="number"
           data-help="Number of items in stock">45</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">On order</div>
      <div class="decoration">:</div>
      <div class="padding"> </div>
      <div class="data" data-tag="on-order"
           data-xpath="/top/data/item/on-order" data-type="number"
           data-help="Number of items on order">1</div>
    </div>
    <div class="line">
      <div class="padding">   </div>
      <div class="label">SKU</div>
      <div class="text">: </div>
      <div class="data" data-tag="sku"
           data-xpath="/top/data/item/sku" data-type="string"
           data-help="Stock Keeping Unit">GRO-000-533</div>
    </div>
